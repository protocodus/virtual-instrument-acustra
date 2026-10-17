#!/usr/bin/env python3
"""Resolve stable versioned output paths and verify compiled build identities."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import zipfile

PRODUCT = "Acustra"
PROJECT = Path(__file__).resolve().parent.parent


def stable_dist_root(project=PROJECT):
    if os.environ.get("DIST_ROOT"):
        return Path(os.environ["DIST_ROOT"]).expanduser().resolve()
    try:
        def git(*args):
            return subprocess.check_output(["git", "-C", str(project), "rev-parse", *args], text=True, stderr=subprocess.DEVNULL).strip()
        top = Path(git("--show-toplevel")).resolve()
        common = Path(git("--path-format=absolute", "--git-common-dir")).resolve()
        return common.parent / project.resolve().relative_to(top) / "dist"
    except (subprocess.CalledProcessError, FileNotFoundError, ValueError):
        return project.resolve() / "dist"


def identity(build, config="Release", formats=("VST3", "Standalone")):
    cache = (build / "CMakeCache.txt").read_text()
    values = {}
    for key, kind, field in (("CMAKE_PROJECT_VERSION", "STATIC", "version"),
            (PRODUCT.upper()+"_BUILD_NUMBER", "STRING", "build_number"),
            (PRODUCT.upper()+"_DISTRIBUTION_VERSION", "INTERNAL", "distribution_version")):
        matches = re.findall(r"^"+key+":"+kind+r"=(.*)$", cache, re.M)
        if len(matches) != 1:
            raise ValueError("CMake cache must contain one valid build number/version identity")
        values[field] = matches[0]
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){2}", values["version"]):
        raise ValueError("CMake cache must contain one valid project version")
    if not re.fullmatch(r"[1-9][0-9]*(?:\.[1-9][0-9]*)?", values["build_number"]):
        raise ValueError("CMake cache must contain one valid build number")
    if values["distribution_version"] != values["version"]+"-build."+values["build_number"]:
        raise ValueError("distribution version must match project version and build number")
    if "BUILD_NUMBER" in os.environ and os.environ["BUILD_NUMBER"] != values["build_number"]:
        raise ValueError("BUILD_NUMBER disagrees with the configured build; rebuild before packaging")
    if "VERSION" in os.environ and os.environ["VERSION"] != values["version"]:
        raise ValueError("VERSION disagrees with the configured version")
    if "GITHUB_RUN_NUMBER" in os.environ and "BUILD_NUMBER" not in os.environ:
        if os.environ["GITHUB_RUN_NUMBER"] != values["build_number"]:
            raise ValueError("CI run identity disagrees with the configured build")
    configured = json.loads((build / (PRODUCT+"-build-identity.json")).read_text())
    for key, value in {"product": PRODUCT, **values}.items():
        if configured.get(key) != value:
            raise ValueError("configured identity mismatch: "+key)
    for fmt in formats:
        compiled = build / (PRODUCT+"_artefacts") / config / fmt / (PRODUCT+"-build-identity.json")
        if json.loads(compiled.read_text()) != configured:
            raise ValueError("compiled identity mismatch; rebuild "+fmt)
    return configured


def output_dir(record, platform, arch):
    if platform not in ("macOS", "Windows", "Linux") or arch not in ("universal", "arm64", "x86_64", "x64"):
        raise ValueError("unsupported platform/architecture")
    return stable_dist_root() / record["version"] / ("build."+record["build_number"]) / (platform+"-"+arch)


def require_new_destination(directory):
    if directory.exists() and any(directory.iterdir()):
        raise ValueError("distribution identity already exists; choose a new build number: "+str(directory))


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""):
            h.update(block)
    return h.hexdigest()


def manifest(record, directory, platform, arch, files):
    stem = PRODUCT+"-"+record["distribution_version"]+"-"+platform+"-"+arch
    artifacts = {path.name: digest(path) for path in files}
    target = directory / (stem+".manifest.json")
    target.write_text(json.dumps({**record, "platform":platform, "architectures":(["arm64", "x86_64"] if arch=="universal" else ["x86_64" if arch=="x64" else arch]),
        "artifacts_sha256":artifacts}, indent=2)+"\n")
    files = [*files, target]
    sums = directory / (stem+"-SHA256SUMS.txt")
    sums.write_text("".join(digest(path)+"  "+path.name+"\n" for path in files))
    return target, sums


def package_desktop(build, platform, config="Release"):
    formats=["VST3", "CLAP", "Standalone"]
    artifacts=build/(PRODUCT+"_artefacts")/config
    machine="x86_64-linux" if platform=="Linux" else "x86_64-win"
    binary=PRODUCT+".so" if platform=="Linux" else PRODUCT+".vst3"
    required=[f"VST3/{PRODUCT}.vst3/Contents/{machine}/{binary}",
              "Standalone/"+PRODUCT+(".exe" if platform=="Windows" else ""),
              "CLAP/"+PRODUCT+".clap"]
    for relative in required:
        f=artifacts/relative
        if not f.is_file() or not f.stat().st_size:
            raise ValueError("missing or empty "+platform+" x64 binary: "+str(f))
    if platform=="Linux" and not os.access(artifacts/required[1],os.X_OK):
        raise ValueError("standalone binary is not executable")
    record=identity(build,config,formats)
    files={f.relative_to(artifacts).as_posix(): f for f in (artifacts/"VST3"/(PRODUCT+".vst3")).rglob("*") if f.is_file()}
    for relative in required[1:]: files[relative]=artifacts/relative
    docs=["LICENSE","THIRD_PARTY_NOTICES.md","ThirdParty/JUCE-LICENSE.md","ThirdParty/CLAP-LICENSE.md",
          "ThirdParty/JUCE-DEPENDENCY-LICENSES.txt","ThirdParty/CC0-1.0.txt","ThirdParty/Eastman-E1D-README.md","ThirdParty/Shinyguitar-README.txt","Assets/SampleBank/manifest.json"]
    files["USER_GUIDE.md"]=PROJECT/"Docs/guide/user-guide.md"
    for doc in docs: files[doc]=PROJECT/doc
    for f in files.values():
        if not f.is_file() or not f.stat().st_size: raise ValueError("missing or empty distribution document: "+str(f))
    dist=output_dir(record,platform,"x64");require_new_destination(dist);dist.mkdir(parents=True,exist_ok=True)
    stem=PRODUCT+"-"+record["distribution_version"]+"-"+platform+"-x64"
    archive=dist/(stem+(".tar.gz" if platform=="Linux" else ".zip"))
    with tempfile.TemporaryDirectory(prefix=".package-",dir=dist) as work:
        staging=Path(work); staged=staging/archive.name
        if platform=="Linux":
            with tarfile.open(staged,"w:gz") as output:
                for relative,f in sorted(files.items()): output.add(f,arcname=relative,recursive=False)
            with tarfile.open(staged) as output: output.getmembers()
        else:
            with zipfile.ZipFile(staged,"w",compression=zipfile.ZIP_DEFLATED) as output:
                for relative,f in sorted(files.items()): output.write(f,relative)
            with zipfile.ZipFile(staged) as output:
                if output.testzip() is not None: raise ValueError("corrupt archive")
        manifest(record,staging,platform,"x64",[staged])
        for f in staging.iterdir(): f.replace(dist/f.name)
    return archive


def package_main(platform):
    """Command line for scripts/package-<platform>.py."""
    parser=argparse.ArgumentParser(description="Package the "+platform+" x64 VST3, CLAP and standalone without changing their host identities.")
    parser.add_argument("--build-dir",type=Path,required=True)
    parser.add_argument("--config",default="Release")
    args=parser.parse_args();print(package_desktop(args.build_dir.resolve(),platform,args.config))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--platform", required=True)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--formats", nargs="+", default=["VST3", "Standalone"])
    parser.add_argument("--field", default="directory", choices=["directory", "source_revision", "source_state"])
    parser.add_argument("--require-new", action="store_true")
    parser.add_argument("--manifest", nargs="*", type=Path)
    args=parser.parse_args()
    record=identity(args.build_dir.resolve(), args.config, args.formats)
    directory=output_dir(record,args.platform,args.arch)
    if args.require_new:
        require_new_destination(directory)
    if args.manifest is not None:
        print(*manifest(record,directory,args.platform,args.arch,args.manifest),sep="\n")
    else:
        print(directory if args.field=="directory" else record[args.field])

if __name__=="__main__":
    main()
