$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'set -eu; mkdir /tmp/title-install-tests; cd /tmp/title-install-tests; g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/app/source /project/tests/title_install_test.cpp -o package-check; ./package-check; mkdir worker; cd worker; g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -D__WIIU__ -I/project/tests/title-install-stubs -I/project/app/source /project/tests/title_install_backend_test.cpp /project/app/source/title_install.cpp -pthread -o job-check; ./job-check'
if ($LASTEXITCODE -ne 0) { throw 'Title package/worker checks failed' }
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" -e UFLOW_LIBRARY_TESTS=title_install_library gcc:14-bookworm sh /project/tools/test-library.sh
if ($LASTEXITCODE -ne 0) { throw 'Title installer library integration checks failed' }
