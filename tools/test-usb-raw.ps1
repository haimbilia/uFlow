$ErrorActionPreference='Stop'
& "$PSScriptRoot/test-mcp-replacements.ps1"
$project=Split-Path $PSScriptRoot -Parent
python (Join-Path $project 'tests/test_raw_hook_scope.py')
if($LASTEXITCODE -ne 0){throw 'Raw hook process-scope regression failed'}
& (Join-Path $PSScriptRoot 'test-raw-rpl.ps1')
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/module/src /project/tests/raw_native_path_test.cpp -o /tmp/native-path && /tmp/native-path'
if($LASTEXITCODE -ne 0){throw 'USB native-path regression failed'}
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" -e UFLOW_LIBRARY_TESTS=raw_source gcc:14-bookworm sh -c 'gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined /project/tests/raw_usb_test.c -o /tmp/raw-usb && /tmp/raw-usb && gcc -std=c11 -DUFLOW_USB_LOADER -I/project/app/vendor/fatfs /project/tests/usb_fatfs_test.c /project/app/vendor/fatfs/ff.c /project/app/vendor/fatfs/ffunicode.c -o /tmp/fat-map && /tmp/fat-map && sh /project/tools/test-library.sh'
if($LASTEXITCODE -ne 0){throw 'USB raw regression failed'}
