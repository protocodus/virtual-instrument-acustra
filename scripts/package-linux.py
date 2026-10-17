#!/usr/bin/env python3
"""Package the Linux x64 VST3, CLAP and standalone without changing their host identities."""
from distribution import package_main

if __name__=="__main__":
    package_main("Linux")
