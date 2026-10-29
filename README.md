# plain

Браузер для GNOME, который делает ровно то, что сказали. Одно окно, не больше
пяти вкладок, никакого интерфейса, кроме страницы, и командная строка по Ctrl+;.
Родное приложение GNOME: GTK 4, libadwaita и WebKitGTK.

Почему он такой, описано в [DESIGN.md](DESIGN.md).

## Установка в Arch Linux

```sh
git clone https://github.com/wolframium-main/plain
cd plain/packaging/aur
makepkg -si
```

PKGBUILD скачивает с GitHub исходники релиза, указанного в нём, и собирает обычный
пакет: удаляется он через `sudo pacman -R plain`. Чтобы обновиться до нового
релиза, выполни `git pull` и снова `makepkg -si`.

## Сборка вручную

Нужны GTK 4, libadwaita, WebKitGTK 6.0 и Rust (cargo).

Arch:

```sh
sudo pacman -S --needed gtk4 libadwaita webkitgtk-6.0 cargo
```

Fedora:

```sh
sudo dnf install gtk4-devel libadwaita-devel webkitgtk6.0-devel cargo
```

Debian / Ubuntu:

```sh
sudo apt install libgtk-4-dev libadwaita-1-dev libwebkitgtk-6.0-dev cargo
```

Сборка и запуск:

```sh
cargo run --release
```

Тесты: `cargo test`.

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

Esc закрывает строку. Когда сайт просит камеру или микрофон, ответ — `y` или `n`.

## Где что лежит

- `~/.local/share/plain/` — куки, данные сайтов и список фильтров рекламы.
- `~/.cache/plain/` — кэш.
- Загрузки — в папке «Загрузки».

## Лицензия

GPL-3.0 или новее, см. [LICENSE](LICENSE).
