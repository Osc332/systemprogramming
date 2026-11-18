# Задача 1.2. Вызов ядра из Python через ctypes

Нужно было собрать функцию из 1.1 в динамическую библиотеку (.dll) и вызвать её из Python через модуль `ctypes`. Плюс разобраться, что будет, если структуру в Python описать неправильно.

## Что лежит в папках

- `agent/core/` — исходники ядра.
  - `core.h` — структура `core_info` и объявление `core_get_info`. Сюда добавлен макрос `CORE_API`.
  - `core_info.c` — реализация функции под Windows.
  - `show_info.c` — программа из 1.1, оставил для проверки.
- `agent/brain/` — скрипт на Python.
  - `brain.py` — загружает DLL и вызывает функцию.

В папке `brain` нет никаких `.c` и `.h`. Все исходники C лежат только в `core`.

## Шаг 1. Добавил CORE_API

MSVC не экспортирует функции из DLL без явного указания. Если этого не сделать, Python выдаст `AttributeError: function not found`.

В `core.h` добавил блок:

```c
#ifdef _WIN32
#define CORE_API __declspec(dllexport)
#else
#define CORE_API
#endif
```

И поставил `CORE_API` перед объявлением функции:
```c
CORE_API int core_get_info(struct core_info *out);
```

## Шаг 2. Собрал DLL

Собирал в **x64 Native Tools Command Prompt for VS 2022**:

```bat
cd C:\SysProg\agent\core
cl /nologo /W4 /LD core_info.c /Fe:sentry_core.dll
```

Появились файлы `sentry_core.dll`, `sentry_core.lib` и `sentry_core.exp`. Python использует только `.dll`, остальные — побочные файлы компоновщика.

## Шаг 3. Проверил таблицу экспорта

```bat
dumpbin /nologo /exports sentry_core.dll
```

В выводе должна быть строка:

```text
ordinal hint RVA      name
      1    0 00001000 core_get_info
```

Имя без подчёркивания в начале. На 64-битном MSVC функции C не декорируются. Если там будет `_core_get_info`, Python её не найдёт.

## Шаг 4. Начал brain.py

Сначала написал минимальную версию в `agent/brain/brain.py`:

```python
import ctypes
import sys
from pathlib import Path

CORE_DIR = Path(__file__).resolve().parent.parent / "core"
LIB_NAME = {"win32": "sentry_core.dll", "darwin": "libsentry_core.dylib"}.get(
    sys.platform, "libsentry_core.so"
)

core = ctypes.CDLL(str(CORE_DIR / LIB_NAME))
print(core)
print(core.core_get_info)
```

Запустил `python brain.py`. На экран вывелись объект CDLL и `<_FuncPtr object ...>`. Значит, DLL загрузилась и функция найдена.

## Шаг 5. Описал структуру

```python
class CoreInfo(ctypes.Structure):
    _fields_ = [
        ("pid", ctypes.c_int64),
        ("cpus", ctypes.c_int32),
        ("page_size", ctypes.c_int32),
        ("host", ctypes.c_char * 256),
    ]
```

Проверил размер:
```python
print(ctypes.sizeof(CoreInfo))
```
Вывело `272`. Значит, поля совпали с C-структурой.

## Шаг 6. Задал сигнатуру

```python
core.core_get_info.argtypes = [ctypes.POINTER(CoreInfo)]
core.core_get_info.restype = ctypes.c_int
```

Без `argtypes` и `restype` ctypes передаёт все аргументы как `int` (4 байта). А указатель на 64-битной системе — 8 байт. Младшие 32 бита он бы передал, старшие отрезал, и получился бы `Access violation`. Поэтому эти две строки обязательны.

## Шаг 7. Проверил, что будет при ошибке в полях

Решил проверить, что пишут в задании. Поменял местами `pid` и `cpus` в `_fields_` и запустил снова. Ошибки не было, но значения стали мусором: `pid` — огромное число, `cpus` — непонятно что, `host` сдвинулся.

**Вывод:** ctypes не сверяет структуру с C. Если ошибиться в порядке или типах, программа не упадёт, а просто прочитает чужие байты.

Вернул правильный порядок.

## Шаг 8. Проверил пустой указатель

Временно заменил вызов в `get_info()` на:

```python
err = core.core_get_info(None)
print(err)
```

Вывело `87`. Это `ERROR_INVALID_PARAMETER` (Windows). Значит, проверка `if (out == NULL)` внутри C сработала. Если бы её не было, `memset(NULL, ...)` уронил бы процесс Python.

Вернул правильный вызов через `ctypes.byref(info)`.

## Шаг 9. Финальный brain.py

```python
"""Мозг агента: загружает ядро на C и получает сведения о системе."""
import ctypes
import json
import sys
from pathlib import Path

CORE_DIR = Path(__file__).resolve().parent.parent / "core"
LIB_NAME = {"win32": "sentry_core.dll", "darwin": "libsentry_core.dylib"}.get(
    sys.platform, "libsentry_core.so"
)


class CoreInfo(ctypes.Structure):
    _fields_ = [
        ("pid", ctypes.c_int64),
        ("cpus", ctypes.c_int32),
        ("page_size", ctypes.c_int32),
        ("host", ctypes.c_char * 256),
    ]


core = ctypes.CDLL(str(CORE_DIR / LIB_NAME))
core.core_get_info.argtypes = [ctypes.POINTER(CoreInfo)]
core.core_get_info.restype = ctypes.c_int


def error_text(code):
    if sys.platform == "win32":
        return ctypes.FormatError(code)
    return "error"


def get_info():
    info = CoreInfo()
    err = core.core_get_info(ctypes.byref(info))
    if err != 0:
        raise OSError(err, error_text(err))
    return {
        "pid": info.pid,
        "cpus": info.cpus,
        "page_size": info.page_size,
        "host": info.host.decode("utf-8"),
    }


if __name__ == "__main__":
    message = {"event": "agent_started", "os": sys.platform, **get_info()}
    print(json.dumps(message, ensure_ascii=False, indent=2))
```

Запуск из папки `brain`:
```bat
python brain.py
```

Получилось:
```json
{
  "event": "agent_started",
  "os": "win32",
  "pid": 15344,
  "cpus": 10,
  "page_size": 4096,
  "host": "DESKTOP-XXXXXXX"
}
```
(`sys.platform` на Windows всегда `"win32"`, даже на 64-битной — это историческое имя.)

## Какие были проблемы

1. Сначала собирал DLL не из той папки. `cl` выдавал `C1083: Не удается открыть файл источник: core_info.c`. Надо было перейти в `C:\SysProg\agent\core`, а я был в `brain`.
2. В `core.h` при копировании попали символы `…` (многоточие) из примеров. MSVC ругался `C3872: "U+a6"`. Пришлось перезаписывать файл через PowerShell и сохранять в UTF-8 with BOM.

## Итог

Python вызывает функцию из DLL, читает структуру 272 байта и печатает JSON. Дальше в 1.3 к `brain.py` добавлю отправку этого JSON в n8n.
