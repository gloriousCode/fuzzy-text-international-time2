.PHONY: c build test lint misc_checks clean config-emery config-gabbro

c: build

build:
	pebble build

test:
	mkdir -p build
	$(CC) -std=c99 -Wall -Wextra -Werror -Isrc \
		tests/text_layout_test.c \
		src/text_layout.c \
		src/text_lines.c \
		src/num2words.c \
		src/strings-ca.c \
		src/strings-de.c \
		src/strings-en_GB.c \
		src/strings-en_US.c \
		src/strings-es.c \
		src/strings-fr.c \
		src/strings-no.c \
		src/strings-sv.c \
		-o build/text_layout_test
	./build/text_layout_test
	$(CC) -std=c99 -Wall -Wextra -Werror -Isrc \
		tests/spy_face_geometry_test.c \
		src/spy_face_geometry.c \
		-o build/spy_face_geometry_test
	./build/spy_face_geometry_test

lint: build

misc_checks: test

clean:
	rm -rf build

config-emery:
	pebble emu-app-config --emulator emery --file resources/configure-fuzzy-text-two.html

config-gabbro:
	pebble emu-app-config --emulator gabbro --file resources/configure-fuzzy-text-two.html

l: c
	deploypebble.sh load build/fuzzy-text-two.pbw
d: c
	deploypebble.sh reinstall build/fuzzy-text-two.pbw
