# esp-button

[![build](https://github.com/mdvorak-iot/esp-button/actions/workflows/build.yml/badge.svg)](https://github.com/mdvorak-iot/esp-button/actions/workflows/build.yml)

Button handler, with software debounce logic and long-press support.

Internally, it is using interrupts and esp_timer for debounce. Does not poll state when idle, 
only while button is being pressed (this is necessary, as it is impossible to disambiguate 
press and release in an interrupt routine).

## Usage

To reference this library by your project, add it as git submodule, using command

```shell
git submodule add https://github.com/mdvorak-iot/esp-button.git components/button
```

and include either of the header files

```c
#include <button.h>
```

For full example, see [button_example_main.c](example/main/button_example_main.c).

## Development

Load `example/CMakeLists.txt` instead of root project. That will load both functional compilable 
example and the library itself.
