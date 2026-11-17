TARGET = freewrite_libretro.so
SRC    = freewrite_libretro.c
CFLAGS_COMMON = -O2 -std=gnu99 -Wall -Wextra -fPIC -fvisibility=hidden

# Desktop build (for testing the logic on your computer)
host: $(SRC) libretro.h
	$(CC) $(CFLAGS_COMMON) -shared -o $(TARGET) $(SRC)

# RG35XX build (32-bit ARM, Cortex-A9). Needs an ARM cross toolchain.
arm: $(SRC) libretro.h
	arm-linux-gnueabihf-gcc $(CFLAGS_COMMON) -mcpu=cortex-a9 -mfpu=neon \
		-mfloat-abi=hard -shared -o $(TARGET) $(SRC)

test: host test_host.c
	$(CC) -O1 -Wall -o test_host test_host.c -ldl
	./test_host ./$(TARGET)
	@echo "--- saved file ---"
	@cat test_saves/*.fountain

clean:
	rm -rf $(TARGET) test_host test_saves shot_*.ppm

# Standalone app (no RetroArch needed). Static, so it runs regardless of the device's libraries.
app-host: app_main.c freewrite_libretro.c libretro.h
	$(CC) -O2 -std=gnu99 -Wall -Wextra -o scriptwriter app_main.c

app: app_main.c freewrite_libretro.c libretro.h
	arm-linux-gnueabihf-gcc -O2 -std=gnu99 -Wall -Wextra -mcpu=cortex-a9 -mfloat-abi=hard \
		-static -o scriptwriter app_main.c
