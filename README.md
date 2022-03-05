# button

[![test](https://github.com/mdvorak/esp-button/actions/workflows/test.yml/badge.svg)](https://github.com/mdvorak/esp-button/actions/workflows/test.yml)

Button handler, with software debounce logic and long-press support.

Internally, it is using interrupts and esp_timer for debounce. Does not poll state when idle, 
only while button is being pressed (this is necessary, as it is impossible to disambiguate 
press and release in an interrupt routine).

## Usage

To reference this library by your project, add it as git submodule, using command

```shell
git submodule add https://github.com/mdvorak/esp-button.git components/button
```

and include either of the header files

```c
#include <button.h>
```

For full example, see [button_example_main.c](example/main/button_example_main.c).

## Development

Prepare [ESP-IDF development environment](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#get-started-get-prerequisites)
.

Configure example application with

```
cd example/
idf.py menuconfig
```

Flash it via (in the example dir)

```
idf.py build flash monitor
```

As an alternative, you can use [PlatformIO](https://docs.platformio.org/en/latest/core/installation.html) to build and
flash the example project.
