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

lint: build

misc_checks: test

clean:
	rm -rf build

config-emery:
	pebble emu-app-config --emulator emery --file resources/configure-fuzzy-text.html

config-gabbro:
	pebble emu-app-config --emulator gabbro --file resources/configure-fuzzy-text.html

l: c
	deploypebble.sh load ~/Pebble/TextWatch/build/TextWatch.pbw
d: c
	deploypebble.sh reinstall  ~/Pebble/TextWatch/build/TextWatch.pbw 
