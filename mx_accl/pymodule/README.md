# Python Runtime API (>= SDK 2p2)

This Python module exposes MxAccl and MxAcclMT, Python bindings for the C++ MX_API library built with pybind11.

## Prerequisites

Before running examples and tests, please complete the following first:
1. [Clone and Build](../../README.md#clone-and-build) section in the main README
2. [setup MX_API Python Module](#setup-mx_api-python-module) below.
3. [Activate virtualenv](#3-activate-virtualenv)

## Setup MX_API Python Module

### 1. Build MX_API pymodule
```bash
cd MX_API/mx_accl/pymodule
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 2. Create symlink
```bash
cd pymodule/examples
# or
cd pymodule/tests

# Link the built Python extension and the memryx binary.
# The "310" substring reflects your Python version (e.g., 3.10).  
# Update it accordingly (e.g., use "311" for Python 3.11).
# 
# Example:
# ln -sv ../build/mxapi.cpython-<python_version>-x86_64-linux-gnu.so
ln -sv ../build/mxapi.cpython-310-x86_64-linux-gnu.so
```

### 3. Activate virtualenv

Activate the virtual environment that includes the memryx Python package.
```bash
python3 -m venv ~/mx
. ~/mx/bin/activate
pip install --upgrade pip wheel
pip install --extra-index-url https://developer.memryx.com/pip memryx
pip install numpy==1.26.4
pip install pytest==8.2.2
```


NOTE: You may see two different mxapi modules. This is expected. There is no conflict between:
- the memryx package installed via pip, and
- the locally built mxapi module from the MX_API repository.

They exist in separate module paths. For testing purposes, we will use the locally built mxapi module.
```python
# Using the memryx package installed via pip:
from memryx import mxapi

# Using the locally built MX_API Python module for testing:
import mxapi
```

### Run examples

Details pls refer to each example file. For instance:
```bash
cd examples
python cartoonizer.py --show
```


### Run tests

#### Basic Test Execution

```bash
cd tests
pytest test_accl.py
# or display verbose log using below command
pytest test_accl.py -s -x -v -o log_cli=true --log-cli-level=DEBUG
```

#### Advanced Pytest Options

For more detailed test execution and debugging, use these flags:

```bash
pytest test_accl.py -s -x -v -o log_cli=true --log-cli-level=DEBUG --local
```

**Flag Explanations:**
- `-s`: Show print statements and output (disable output capture)
- `-x`: Stop after first failure
- `-v`: Verbose output
- `-o log_cli=true`: Enable live logging to console
- `--log-cli-level=DEBUG`: Set logging level to DEBUG
- `-k <expression>`: Run only tests matching the given expression
- `--local`: Run in local mode. If not specified, run in shared mode

**Common Test Selection Examples**

```bash
# Run all tests in a specific class
pytest test_accl.py::TestAccl

# Run specific test case
pytest test_accl.py::TestAccl::test_basic

# Run only tests matching the given expression 
# (e.g. Matches both TestAccl::test_basic & TestAcclMT::test_basic)
pytest test_accl.py -k "test_basic"

# Run tests in local mode
pytest test_accl.py --local
```


## Deprecated Python Runtime API (< SDK 2p2)
Pls refer deprecated Python API in [MIX repo](https://github.com/memryx/MIX/tree/development/runtime/tests).
