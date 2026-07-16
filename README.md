# DARMA/LB

This repo implements scalable load balancers for workloads independently of runtime (can run on an arbitrary communicator: e.g., MPI, [DARMA/vt](https://github.com/DARMA-tasking/vt/)).

## Dependencies
- `vt` needs `magistrate`
- `LB` needs `vt` and `comm`

## Building with *vt*

You need [magistrate](https://github.com/DARMA-tasking/magistrate/) and [vt](https://github.com/DARMA-tasking/vt/)
to use `vt` as a communicator for `LB`. Check out `vt`'s [build script](https://darma-tasking.github.io/docs/html/vt-build.html#using-the-build-script)
or use CMake directly:
```
# you can use development version of vt and magistrate
git clone https://github.com/DARMA-tasking/magistrate.git
cmake -S magistrate -B magistrate/build \
  -DCMAKE_INSTALL_PREFIX=magistrate/build/install
cmake --build magistrate/build --target install

git clone https://github.com/DARMA-tasking/vt.git
cmake -S vt -B vt/build                      \
  -DCMAKE_INSTALL_PREFIX=vt/build/install    \
  -Dmagistrate_ROOT=magistrate/build/install \
  -Dvt_build_examples=0                      \ # skip optional targets to speed up build
  -Dvt_build_tests=0                         \
  -Dvt_build_tools=0
cmake --build vt/build --target install
```

`LB` requires `vt` and `comm` installation directory:
```bash
git clone git@github.com:DARMA-tasking/comm.git

cmake -S LB -B LB/build \
  -DVT_LB_COMM_ROOT=comm \
  -Dvt_ROOT=vt/build/install

cmake --build LB/build
```

## Tests
```bash
ctest --test-dir LB/build
```
