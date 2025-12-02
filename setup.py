"""
TraceSmith - GPU Profiling & Replay System

Installation:
    pip install .

Development installation:
    pip install -e .
"""

import os
import sys
import subprocess
from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        
        # Required for auto-detection of auxiliary "native" libs
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        cfg = 'Debug' if self.debug else 'Release'

        cmake_args = [
            f'-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}',
            f'-DPYTHON_EXECUTABLE={sys.executable}',
            f'-DCMAKE_BUILD_TYPE={cfg}',
            '-DTRACESMITH_BUILD_TESTS=OFF',
            '-DTRACESMITH_BUILD_EXAMPLES=OFF',
            '-DTRACESMITH_BUILD_CLI=OFF',
            '-DTRACESMITH_BUILD_PYTHON=ON',
        ]

        build_args = ['--config', cfg]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp)
        subprocess.check_call(['cmake', '--build', '.'] + build_args, cwd=self.build_temp)


# Read long description from README
this_directory = Path(__file__).parent
long_description = (this_directory / "README.md").read_text()

setup(
    name='tracesmith',
    version='0.1.0',
    author='TraceSmith Authors',
    author_email='tracesmith@example.com',
    description='GPU Profiling & Replay System',
    long_description=long_description,
    long_description_content_type='text/markdown',
    url='https://github.com/your-org/tracesmith',
    license='Apache-2.0',
    
    packages=find_packages(where='python'),
    package_dir={'': 'python'},
    
    ext_modules=[CMakeExtension('tracesmith._tracesmith')],
    cmdclass={'build_ext': CMakeBuild},
    
    python_requires='>=3.7',
    install_requires=[],
    extras_require={
        'dev': [
            'pytest>=6.0',
            'numpy',
        ],
    },
    
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'Intended Audience :: Science/Research',
        'License :: OSI Approved :: Apache Software License',
        'Operating System :: OS Independent',
        'Programming Language :: C++',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Topic :: Scientific/Engineering',
        'Topic :: Software Development :: Debuggers',
        'Topic :: System :: Monitoring',
    ],
    
    keywords='gpu profiling tracing cuda rocm debugging replay',
    
    entry_points={
        'console_scripts': [
            'tracesmith=tracesmith.cli:main',
        ],
    },
    
    zip_safe=False,
)
