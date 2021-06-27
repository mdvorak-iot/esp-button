# esp-button

![platformio build](https://github.com/mdvorak-iot/esp-button/workflows/platformio%20build/badge.svg)

Button handler, with software debounce logic and long-press support.

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
