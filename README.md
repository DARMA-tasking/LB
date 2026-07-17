# DARMA/LB

This repo implements scalable load balancers for workloads independently of runtime (can run on an arbitrary communicator: e.g., MPI, [DARMA/vt](https://github.com/DARMA-tasking/vt/)).

## Dependencies

LB requires MPI and an installed copy of
[DARMA/comm](https://github.com/DARMA-tasking/comm). The VT backend is optional.
LB discovers comm with `find_package(comm CONFIG REQUIRED)`; it does not build
comm's source tree or use comm's private test files.

## Quick start: MPI backend

The commands below assume that `LB` and `comm` are cloned next to each other.

```bash
git clone https://github.com/DARMA-tasking/comm.git

cmake -S comm -B comm/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/comm/install" \
  -Dvt_backend_enabled=OFF
cmake --build comm/build --target install

cmake -S LB -B LB/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/comm/install" \
  -Dvt_backend_enabled=OFF
cmake --build LB/build
```

`CMAKE_PREFIX_PATH` points CMake at comm's installation prefix. If comm is
installed in a standard system location, that option can be omitted. You can
also set `comm_DIR` directly to the directory containing `commConfig.cmake`,
for example `-Dcomm_DIR=/path/to/comm/install/cmake`; its transitive dependency
packages must still be discoverable by CMake.

## Building with the VT backend

First build and install VT, then build comm with VT enabled. See the
[VT build documentation](https://darma-tasking.github.io/docs/html/vt-build.html#using-the-build-script)
for the VT installation steps.

```bash
cmake -S comm -B comm/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/comm/install" \
  -DCMAKE_PREFIX_PATH="$PWD/vt/build/install" \
  -Dvt_backend_enabled=ON
cmake --build comm/build --target install

cmake -S LB -B LB/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/comm/install;$PWD/vt/build/install" \
  -Dvt_backend_enabled=ON
cmake --build LB/build
```

## Tests and installation

```bash
ctest --test-dir LB/build --output-on-failure
cmake --install LB/build --prefix "$PWD/LB/install"
```

To consume an installed LB from another CMake project:

```cmake
find_package(vtLB CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE vt::lib::vt-lb)
```

When configuring that project, add both installation prefixes to
`CMAKE_PREFIX_PATH`, for example:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/LB/install;/path/to/comm/install"
```
