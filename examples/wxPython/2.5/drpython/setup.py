"""
Script for building the example.

Usage:
    python setup.py py2app
"""
import os

from setuptools import setup

VERSION = "3.10.1"
PATH = f"drpython-{VERSION}"
script = []
includes = []
data_files = []
for name in os.listdir(PATH):
    if name.startswith(".") or name == "CVS":
        continue
    fn = os.path.join(PATH, name)
    mod, ext = os.path.splitext(name)
    if ext == ".pyw":
        script.append(fn)
    elif ext == ".py":
        includes.append(mod)
    elif ext == ".pyc":
        continue
    else:
        data_files.append(fn)

setup(
    app=script,
    data_files=data_files,
    options={
        "py2app": {
            "includes": includes,
            "argv_emulation": True,
        }
    },
    setup_requires=["py2app"],
)
