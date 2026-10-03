CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
CORE = src/domain/or_lifecycle.c src/domain/or_effects.c src/storage/or_config.c src/presentation/or_notice.c src/presentation/or_boss.c src/app/or_app.c
PLATFORM = src/platform/tef/or_probe.c src/platform/tef/or_read.c src/platform/tef/or_hooks.c src/storage/or_log.c src/entry/mod.c mod-api/tefkernel/tef_api_imp.c
INC = -Iinclude -Imod-api
.PHONY: all test sanitize clean host
all: test host
build:
	mkdir -p build
test: build
	$(CC) $(CFLAGS) $(INC) tests/test_core.c $(CORE) -lm -o build/test_core
	./build/test_core
	$(CC) $(CFLAGS) $(INC) -Isrc/platform/tef tests/test_platform.c $(CORE) src/platform/tef/or_probe.c src/platform/tef/or_read.c src/platform/tef/or_hooks.c mod-api/tefkernel/tef_api_imp.c -lm -o build/test_platform
	./build/test_platform
host: build
	$(CC) $(CFLAGS) $(INC) -fPIC -shared $(CORE) $(PLATFORM) -Wl,-z,defs -lm -o build/libOriginRewrite.host.so
sanitize: build
	$(CC) -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(INC) tests/test_core.c $(CORE) -lm -o build/test_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./build/test_sanitize
	$(CC) -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(INC) -Isrc/platform/tef tests/test_platform.c $(CORE) src/platform/tef/or_probe.c src/platform/tef/or_read.c src/platform/tef/or_hooks.c mod-api/tefkernel/tef_api_imp.c -lm -o build/test_platform_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./build/test_platform_sanitize
clean:
	rm -rf build
