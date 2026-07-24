import os
from setuptools import setup, find_packages

# Read README.md for long description if available
long_description = ""
if os.path.exists("README.md"):
    with open("README.md", "r", encoding="utf-8") as f:
        long_description = f.read()

setup(
    name="py3dgame",
    version="0.2.0",
    author="PythonCoders45",
    description="A high-performance 3D engine with Jolt Physics, NavMesh AI, and QUIC Networking.",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/PythonCoders45/py3dgame",
    packages=find_packages(),
    include_package_data=True,
    package_data={
        "": ["*.so", "*.dll", "*.dylib"],
    },
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: C++",
        "Topic :: Games/Entertainment",
        "Topic :: Software Development :: Libraries",
    ],
    python_requires=">=3.8",
)
