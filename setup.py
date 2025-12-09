"""
TraceSmith - GPU Profiling & Replay System

Installation:
    pip install .

Platform-specific installation:
    # Auto-detect (default)
    pip install .
    
    # CUDA/CUPTI (NVIDIA)
    TRACESMITH_CUDA=1 pip install .
    
    # ROCm (AMD)
    TRACESMITH_ROCM=1 pip install .
    
    # Metal (Apple)
    TRACESMITH_METAL=1 pip install .

With CuPy for real GPU profiling (Python CLI):
    pip install .[cuda12]    # CUDA 12.x
    pip install .[cuda11]    # CUDA 11.x
    pip install .[cuda118]   # CUDA 11.8
    pip install .[cuda120]   # CUDA 12.0

Other extras:
    pip install .[visualization]  # matplotlib, plotly
    pip install .[torch]          # PyTorch integration
    pip install .[dev]            # Development tools
    pip install .[all]            # All extras

Development installation:
    pip install -e .
"""

import os
import sys
import subprocess
import shutil
from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext


def detect_cuda():
    """Detect CUDA installation."""
    # Check for nvcc
    if shutil.which('nvcc'):
        return True
    # Check CUDA_HOME
    cuda_home = os.environ.get('CUDA_HOME') or os.environ.get('CUDA_PATH')
    if cuda_home and os.path.exists(cuda_home):
        return True
    # Check common paths
    for path in ['/usr/local/cuda', '/opt/cuda']:
        if os.path.exists(path):
            return True
    return False


def detect_rocm():
    """Detect ROCm installation."""
    rocm_path = os.environ.get('ROCM_PATH', '/opt/rocm')
    return os.path.exists(rocm_path)


def detect_metal():
    """Detect Metal (macOS)."""
    return sys.platform == 'darwin'


def detect_perfetto_sdk():
    """Check if Perfetto SDK files exist."""
    perfetto_dir = Path(__file__).parent / 'third_party' / 'perfetto'
    perfetto_h = perfetto_dir / 'perfetto.h'
    perfetto_cc = perfetto_dir / 'perfetto.cc'
    return perfetto_h.exists() and perfetto_cc.exists()


def find_ninja():
    """Find ninja build tool."""
    return shutil.which('ninja') is not None


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        cfg = 'Debug' if self.debug else 'Release'

        # Base CMake arguments
        cmake_args = [
            f'-DTRACESMITH_PYTHON_OUTPUT_DIR={extdir}',
            f'-DPYTHON_EXECUTABLE={sys.executable}',
            f'-DCMAKE_BUILD_TYPE={cfg}',
            '-DTRACESMITH_BUILD_TESTS=OFF',
            '-DTRACESMITH_BUILD_EXAMPLES=OFF',
            '-DTRACESMITH_BUILD_CLI=OFF',
            '-DTRACESMITH_BUILD_PYTHON=ON',
        ]

        # Use Ninja if available (faster builds)
        if find_ninja():
            cmake_args.append('-GNinja')
            print("TraceSmith: Using Ninja build system")

        # Platform selection via environment variables
        enable_cuda = os.environ.get('TRACESMITH_CUDA', '').lower() in ('1', 'true', 'on', 'yes')
        enable_rocm = os.environ.get('TRACESMITH_ROCM', '').lower() in ('1', 'true', 'on', 'yes')
        enable_metal = os.environ.get('TRACESMITH_METAL', '').lower() in ('1', 'true', 'on', 'yes')
        auto_detect = os.environ.get('TRACESMITH_AUTO', '1').lower() in ('1', 'true', 'on', 'yes')

        # Auto-detect if no platform explicitly specified
        if not (enable_cuda or enable_rocm or enable_metal) and auto_detect:
            if detect_cuda():
                enable_cuda = True
                print("TraceSmith: Auto-detected CUDA")
            elif detect_rocm():
                enable_rocm = True
                print("TraceSmith: Auto-detected ROCm")
            elif detect_metal():
                enable_metal = True
                print("TraceSmith: Auto-detected Metal")
            else:
                print("TraceSmith: No GPU platform detected, building base package")

        # Add platform-specific CMake options
        if enable_cuda:
            cmake_args.append('-DTRACESMITH_ENABLE_CUDA=ON')
            # Add CUDA paths if set
            cuda_home = os.environ.get('CUDA_HOME') or os.environ.get('CUDA_PATH')
            if cuda_home:
                cmake_args.append(f'-DCUDA_TOOLKIT_ROOT_DIR={cuda_home}')
            print("TraceSmith: Building with CUDA/CUPTI support")
        else:
            cmake_args.append('-DTRACESMITH_ENABLE_CUDA=OFF')

        if enable_rocm:
            cmake_args.append('-DTRACESMITH_ENABLE_ROCM=ON')
            rocm_path = os.environ.get('ROCM_PATH', '/opt/rocm')
            cmake_args.append(f'-DROCM_PATH={rocm_path}')
            print("TraceSmith: Building with ROCm support")
        else:
            cmake_args.append('-DTRACESMITH_ENABLE_ROCM=OFF')

        if enable_metal:
            cmake_args.append('-DTRACESMITH_ENABLE_METAL=ON')
            print("TraceSmith: Building with Metal support")
        else:
            cmake_args.append('-DTRACESMITH_ENABLE_METAL=OFF')

        # Perfetto SDK support (only enable if SDK files exist)
        perfetto_sdk_exists = detect_perfetto_sdk()
        enable_perfetto = os.environ.get('TRACESMITH_PERFETTO', '1').lower() in ('1', 'true', 'on', 'yes')
        
        if enable_perfetto and perfetto_sdk_exists:
            cmake_args.append('-DTRACESMITH_USE_PERFETTO_SDK=ON')
            print("TraceSmith: Building with Perfetto SDK (protobuf export)")
        else:
            cmake_args.append('-DTRACESMITH_USE_PERFETTO_SDK=OFF')
            if enable_perfetto and not perfetto_sdk_exists:
                print("TraceSmith: Perfetto SDK files not found, building without protobuf export")
                print("TraceSmith: (perfetto.h and perfetto.cc required in third_party/perfetto/)")

        # Additional build arguments
        build_args = ['--config', cfg]
        
        # Parallel build
        cpu_count = os.cpu_count() or 1
        if 'CMAKE_BUILD_PARALLEL_LEVEL' not in os.environ:
            build_args += ['-j', str(cpu_count)]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        print(f"TraceSmith: CMake args: {' '.join(cmake_args)}")
        print(f"TraceSmith: Building with {cpu_count} parallel jobs")
        
        # Run CMake configure with error output capture
        try:
            result = subprocess.run(
                ['cmake', ext.sourcedir] + cmake_args,
                cwd=self.build_temp,
                capture_output=True,
                text=True
            )
            if result.stdout:
                print(result.stdout)
            if result.returncode != 0:
                print("TraceSmith: CMake configuration failed!")
                if result.stderr:
                    print("TraceSmith: CMake stderr:")
                    print(result.stderr)
                raise subprocess.CalledProcessError(result.returncode, 'cmake')
        except FileNotFoundError:
            raise RuntimeError(
                "CMake not found. Please install CMake >= 3.16:\n"
                "  - Linux: apt install cmake or yum install cmake3\n"
                "  - macOS: brew install cmake\n"
                "  - Windows: choco install cmake or download from cmake.org"
            )
        
        # Run CMake build
        subprocess.check_call(['cmake', '--build', '.'] + build_args, cwd=self.build_temp)


# Read long description from README
this_directory = Path(__file__).parent
long_description = (this_directory / "README.md").read_text()

setup(
    name='tracesmith',
    version='0.8.3',
    author='Xingqiang Chen',
    author_email='chenxingqiang@gmail.com',
    description='Cross-platform GPU Profiling & Replay System',
    long_description=long_description,
    long_description_content_type='text/markdown',
    url='https://github.com/chenxingqiang/TraceSmith',
    project_urls={
        'Documentation': 'https://github.com/chenxingqiang/TraceSmith#readme',
        'Source': 'https://github.com/chenxingqiang/TraceSmith',
        'Tracker': 'https://github.com/chenxingqiang/TraceSmith/issues',
    },
    license='Apache-2.0',
    
    packages=find_packages(where='python'),
    package_dir={'': 'python'},
    
    ext_modules=[CMakeExtension('tracesmith._tracesmith')],
    cmdclass={'build_ext': CMakeBuild},
    
    python_requires='>=3.8',
    install_requires=[],
    extras_require={
        'dev': [
            'pytest>=6.0',
            'numpy',
            'build',
            'twine',
        ],
        'cuda': [],  # CUDA dependencies (none required, system CUDA used)
        'rocm': [],  # ROCm dependencies
        'torch': [
            'torch>=1.9',
        ],
        'visualization': [
            'matplotlib',
            'plotly',
        ],
        # CuPy for real GPU profiling in Python CLI
        'cuda11': [
            'cupy-cuda11x>=12.0.0',
        ],
        'cuda12': [
            'cupy-cuda12x>=12.0.0',
        ],
        'cuda118': [
            'cupy-cuda118>=12.0.0',
        ],
        'cuda120': [
            'cupy-cuda12x>=12.0.0',
        ],
        # All extras for full functionality
        'all': [
            'numpy',
            'matplotlib',
            'plotly',
        ],
    },
    
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'Intended Audience :: Science/Research',
        'License :: OSI Approved :: Apache Software License',
        'Operating System :: OS Independent',
        'Operating System :: POSIX :: Linux',
        'Operating System :: MacOS :: MacOS X',
        'Operating System :: Microsoft :: Windows',
        'Programming Language :: C++',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: Python :: 3.12',
        'Topic :: Scientific/Engineering',
        'Topic :: Scientific/Engineering :: Artificial Intelligence',
        'Topic :: Software Development :: Debuggers',
        'Topic :: System :: Monitoring',
        'Environment :: GPU :: NVIDIA CUDA',
    ],
    
    keywords='gpu profiling tracing cuda rocm metal debugging replay perfetto memory-profiler frame-capture cupti',
    
    entry_points={
        'console_scripts': [
            'tracesmith-cli=tracesmith.cli:main',
        ],
    },
    
    zip_safe=False,
)
