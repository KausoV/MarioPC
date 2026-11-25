# Лицензии и авторство

| Компонент | Лицензия | Копия текста | Исходники |
|---|---|---|---|
| SMB-Vanilla, нативное игровое ядро и растровый рендер | GNU GPL v3 | SMB-Vanilla-GPL-3.0.txt | source/SMB-Vanilla |
| Nes_Snd_Emu, Blip_Buffer и NES APU | GNU LGPL v2.1 | Nes_Snd_Emu-LGPL-2.1.txt | source/SMB-Vanilla/Nes_Snd_Emu |
| SDL 3.4.18 | zlib | SDL3-zlib.txt | dependencies/SDL3-3.4.18 |
| Dear ImGui 1.92.9b, меню настроек | MIT | Dear-ImGui-MIT.txt | dependencies/imgui-1.92.9b |
| Noto Sans, шрифт меню с кириллицей | SIL Open Font License 1.1 | NotoSans-OFL-1.1.txt | dependencies/NotoSans |

Проект [SMB-Vanilla](https://github.com/nukep/SMB-Vanilla) разработан Danny Spencer/nukep. Он опирается на документированную дизассемблировку Super Mario Bros. автора doppelganger. Commit игрового ядра: 20f4e02614412f9dbd407dd4e4fab4ade5eb79da.

[Nes_Snd_Emu](https://github.com/jamesathey/Nes_Snd_Emu) происходит от звуковых библиотек Shay Green; сведения о последующих авторах сохранены в исходниках. Commit: dc09f123f9d5083b40c6710aa991a96b4e711e6c. [SDL](https://github.com/libsdl-org/SDL) — Sam Lantinga и участники проекта.

[Dear ImGui](https://github.com/ocornut/imgui) — Omar Cornut и участники проекта. [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic) — The Noto Project Authors; шрифт встроен в executable, его уведомление и полный текст OFL сохранены.

Windows frontend и интеграция «Искры» и предшествующей версии Mario PC являются локальными изменениями и поставляются вместе с исходниками как часть GPL-3.0-программы. Изменённые upstream-файлы перечислены в docs/PORT_NOTES.md. Оригинальные уведомления и тексты лицензий сохранены.

Игровая графика, уровни, музыка и русский перевод взяты из файла пользователя. Они не являются свободными ресурсами SMB-Vanilla; лицензии программных библиотек не распространяются автоматически на эти данные. Русский перевод обозначен исходным именем ROM как Guyver, 24.11.2016.
Лесной декор включает 16 спрайтов Pixel Art Atlas (CC0-1.0). [Манифест исходных ячеек](../assets/iskra/atlas/manifest.json).
