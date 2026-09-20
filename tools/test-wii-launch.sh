#!/bin/sh
set -eu
testdir=$(mktemp -d /tmp/uflow-wii-test.XXXXXX)
g++ -std=c++20 -Wall -Wextra -Werror -O2 -DUFLOW_HOST_TEST -I/project/app/source \
    /project/tests/wii_launch_test.cpp /project/app/source/wii_launch.cpp -o "$testdir/check"
(cd "$testdir" && ./check)
g++ -std=c++20 -Wall -Wextra -Werror -O2 -I/project/tests/wii-stubs -I/project/wii \
    /project/tests/wii_return_test.cpp /project/wii/wii_return.cpp -o "$testdir/return-check"
"$testdir/return-check"
g++ -std=c++20 -Wall -Wextra -Werror -O2 -I/project/tests/wii-stubs -I/project/app/source \
    /project/tests/wii_remote_test.cpp -o "$testdir/remote-check"
"$testdir/remote-check"
g++ -std=c++20 -Wall -Wextra -Werror -O2 -I/project/wii \
    /project/tests/gc_return_stub_test.cpp -o "$testdir/gc-return-check"
"$testdir/gc-return-check"
g++ -std=c++20 -Wall -Wextra -Werror -O2 -I/project/app/source \
    /project/tests/startup_test.cpp /project/app/source/settings.cpp -o "$testdir/startup-check"
(cd "$testdir" && ./startup-check)

g++ -std=c++20 -Wall -Wextra -Werror -O2 -I/project/app/source \
    /project/tests/return_to_uflow_test.cpp -o "$testdir/uflow-return-check"
"$testdir/uflow-return-check"
g++ -std=c++20 -Wall -Wextra -Werror -O2 -DUFLOW_HOST_TEST -I/project/app/source -I/project/wii -I/project/tests \
    /project/tests/usbloader_entry_test.cpp /project/wii/usbloader_entry.cpp -o "$testdir/usb-check"
(cd "$testdir" && ./usb-check)
