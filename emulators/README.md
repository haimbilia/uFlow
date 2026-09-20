# Build dependencies

NooDS is maintained as an external checkout, not an unconfigured git submodule.
To recreate the tested source:

```sh
git clone https://github.com/Hydr8gon/NooDS.git emulators/NooDS
git -C emulators/NooDS checkout 4cb1439c330248b26cdf5f8dbcfa4e5ac53dcda0
git -C emulators/NooDS apply ../noods.patch
git clone https://github.com/wiiu-env/libmocha.git native-installer/vendor/libmocha
git -C native-installer/vendor/libmocha checkout 50fefdf8307a875c63bdbdcf6c973779d4ddac92
```

NooDS's patch adds the uFlow launch/return integration. Its upstream license
remains applicable. Regenerate the patch with `tools/export-dependency-patches.py`
after changing this checkout. libmocha is unmodified at the pinned revision.
