# plain

Браузер для Linux, который делает ровно то, что сказали. Одно окно, не больше
пяти вкладок, никакого интерфейса, кроме страницы, и командная строка по Ctrl+;.

Почему он такой, описано в [DESIGN.md](DESIGN.md).

## Установка в Arch Linux

Из AUR:

```sh
yay -S plain
```

Или из этого дерева исходников:

```sh
cd packaging/arch
makepkg -si
```

Удаление: `sudo pacman -R plain`. После изменений в коде увеличь `pkgrel`
в `PKGBUILD` и снова выполни `makepkg -si`.

## Сборка вручную

Нужны Qt 6.4 или новее с модулем WebEngine, CMake, компилятор C++17 и шрифты
IBM Plex (без них панель будет системным моноширинным шрифтом).

Debian / Ubuntu:

```sh
sudo apt install cmake g++ qt6-base-dev qt6-webengine-dev libgl-dev fonts-ibm-plex
```

Fedora:

```sh
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtwebengine-devel \
    ibm-plex-mono-fonts ibm-plex-sans-fonts
```

Сборка и установка:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

Сделать браузером по умолчанию:

```sh
xdg-settings set default-web-browser plain.desktop
```

## Использование

Ctrl+; (клавиша с двоеточием) открывает строку. Введи адрес, текст для поиска в Google или команду:

```
:new [адрес]   новая вкладка          :find текст   поиск по странице
:tabs          список вкладок         :zoom 125     масштаб
:tab 2         перейти на вкладку     :url          скопировать адрес
:close [2]     закрыть вкладку        :devtools     инструменты разработчика
:back :forward :reload                :quit         выйти
:proceed       открыть страницу с недействительным сертификатом
```

Esc закрывает строку.

## Где что лежит

- `~/.local/share/plain/` — куки, кэш и список фильтров рекламы.
- Загрузки — в системной папке загрузок (обычно `~/Downloads`).

## Тесты

```sh
cmake -S . -B build -DPLAIN_BUILD_TESTS=ON
cmake --build build -j
QT_QPA_PLATFORM=offscreen \
QTWEBENGINE_CHROMIUM_FLAGS=--use-fake-device-for-media-stream \
./build/test_plain
```

Для тестов сертификатов нужны `openssl` и `python3`. Проверено с Qt 6.4
(Ubuntu 24.04) и Qt 6.10.

## Лицензия

GPL-3.0 или новее, см. [LICENSE](LICENSE).
