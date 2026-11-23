> Исторические отчёты и снимки, упомянутые ниже, не входят в компактный пакет исходников. Актуальная краткая проверка выпуска: [RELEASE_CHECKS.json](RELEASE_CHECKS.json).

# Mario PC: происхождение и границы точности

## Состав порта

Mario PC — Windows x64-приложение с игровыми правилами, скомпилированными в нативный код из C-декомпиляции [SMB-Vanilla](https://github.com/nukep/SMB-Vanilla/tree/20f4e02614412f9dbd407dd4e4fab4ade5eb79da). В игровом цикле нет интерпретатора инструкций 6502. Визуальное представление формируется растровым рендером ядра, а звуковые регистры обрабатывает Nes_Snd_Emu. SDL3 обеспечивает окно, ввод, показ RGB-кадров и вывод звука.

Исходный локальный файл: Super Mario Bros. (JU) [!] [T+Rus Guyver (24.11.2016)].nes. SHA-256: 4ad631a5bfead97819b64bef2702b6c53c38b15f38d31f047f5f5f3be2b17260; MD5: 0b24529dceb4cd1da0ca6e1575681e76. Размер 40976 байт: заголовок iNES, 32768 байт PRG и 8192 байта CHR; mapper 0, вертикальное отражение.

Предоставленный файл не изменялся. Его полная копия встроена в generated/game_data.h и исполняемый файл. Содержащиеся в PRG машинные инструкции также остаются байтами в этой копии, но процессорный интерпретатор их не исполняет: C-ядро использует адресуемые таблицы графики, уровней, музыки и интерфейса. Исходный ROM не требуется при запуске. Это не универсальный проигрыватель произвольных .nes.

## Изменения для Windows

Изменённые исходники upstream обозначены данным документом; оригинальная версия указана выше. Основная физика, правила и русские таблицы не заменялись.

| Файл | Изменение и причина |
|---|---|
| source/SMB-Vanilla/src/smbcore/base.h | Макросы error/warning переименованы в smb_error/smb_warning для устранения конфликтов заголовков; проверка builtin_ctz ограничена GCC/Clang, а MSVC использует переносимую ветку |
| source/SMB-Vanilla/src/smbcore/interface.h | Обновлены имена диагностических макросов; joy1/joy2 вызывают void-callback отдельно от return для переносимого C |
| source/SMB-Vanilla/src/smbcore/types.h | Обновлено имя диагностического макроса переполнения метатайлов |
| source/SMB-Vanilla/src/smbcore/common_sound.c | Обновлено имя диагностического макроса в ветке SMB2J |
| source/SMB-Vanilla/src/platform/audio_sdl.cpp | Очистка временного буфера и NES audio object при ошибке инициализации SDL3, открытии audio stream или нехватке памяти |
| src/mario_main.cpp | Windows/SDL3 frontend: встроенные данные, шаг NTSC по накопленному времени, пауза при открытом меню и потере фокуса, горячие клавиши и снимок полного окна |
| src/pc_settings.h, src/pc_settings.cpp | Привязки клавиатуры и геймпада, hotplug и выбор устройства, перечисление мониторов и реальных видеорежимов, проверяемое применение видео и атомарная запись INI |
| src/settings_menu.h, src/settings_menu.cpp | Русское меню Dear ImGui: игра, экран, клавиатура и геймпад; игровые действия, захват клавиш/кнопок, разрешение конфликтов, подтверждение или возврат видеорежима |
| source/SMB-Vanilla/src/smbcore/mario.h, smbcore.c | Версионный снимок SMB1: переносимые данные состояния и смещения указателей, проверка до восстановления, обновление таблиц рендера |
| src/game_save.h, src/game_save.cpp | Один игровой слот, контроль размера/версии/данных/checksum, сохранение последнего RGB-кадра, атомарная запись |
| generated/ui_font.h | Встроенный Noto Sans; внешнего файла шрифта при запуске нет |
| CMakeLists.txt | Сборка MSVC x64, SDL3, статический CRT, UTF-8, библиотеки ядра/рендера/звука/настроек/ImGui и проверочные executables |

Семантика signed integer overflow сохраняется флагом /UndefIntOverflow- для MSVC и -fwrapv для GCC/Clang. Звуковая библиотека используется на закреплённом upstream commit dc09f123f9d5083b40c6710aa991a96b4e711e6c.

## Меню настроек и устройства

SDL3 передаёт события Dear ImGui и менеджеру геймпадов. Меню держит черновик настроек отдельно от действующих значений. Изменение монитора, размера или режима запускает подтверждение на 15 секунд; отмена и тайм-аут возвращают прежнее видео. Настройки записываются только после подтверждения. Неудачное применение видео пытается восстановить предыдущий режим. Несохранённый черновик закрывается крестиком, Esc или F1.

Действующие привязки читает readControls(); каждому действию доступны основная и вторая клавиша, одна кнопка геймпада или триггер. Левый стик добавляет направления с настраиваемой мёртвой зоной. Gamepads обрабатывает реальные SDL add/remove/remap-события, выбирает предпочтительный GUID и доступное устройство при отключении. При выходе из меню ввод ждёт отпускания кнопок, чтобы назначение или закрывшая меню кнопка не сработала в игре. Физическая кнопка Start зарезервирована для открытия меню и исключена из настраиваемых игровых привязок; по умолчанию Start/pause ядра остаётся на Enter. Guide можно назначать обычным действиям.

Настройки находятся по SDL_GetPrefPath("MarioPC", "MarioPC")/settings.ini. Формат UTF-8 INI версии 1; неизвестные будущие поля пропускаются, некорректные значения заменяются безопасными. Запись выполняется через уникальный временный файл и атомарный MoveFileExW с заменой. Перечень разрешений полного экрана берётся из SDL_GetFullscreenDisplayModes; размер/монитор подтверждаются после SDL_SyncWindow. Неподдерживаемый режим не сохраняется как успешно применённый.

Dear ImGui 1.92.9b собирается статически с SDL3 и SDLRenderer3 backends. Noto Sans встроен в generated/ui_font.h. Новых runtime DLL или внешнего шрифта нет: для запуска по-прежнему нужны MarioPC.exe и SDL3.dll. Лицензии MIT/OFL и авторство включены в licenses.

## Игровое меню и сохранение

Esc, F1 и физическая кнопка Start на геймпаде открывают вкладку «Игра»; прежние вкладки настроек сохраняют номера 0/1/2, игровая вкладка имеет номер 3. SettingsMenu возвращает MenuAction, а frontend выполняет продолжение, запуск с титульного экрана, сохранение, загрузку, возврат в меню или выход. «Выйти в меню» требует подтверждения; сохранение при этом остаётся. Меню полностью останавливает SMB_tick, а после закрытия ввод ждёт отпускания кнопок.

SMB_snapshot_export/import в C-ядре используют фиксированный little-endian формат SMB1 версии 1. Снимок содержит RAM 64 КиБ, CHR 8 КиБ, PPU RAM 16 КиБ, скроллинг и остальные скалярные поля ядра. AreaData, EnemyData и MusicData хранятся как смещения в RAM; адреса функций и userdata не записываются. Импорт проверяет размер, версию, checksum, допустимость смещений и соответствие неизменяемых игровых данных до любого изменения состояния. Живые callbacks сохраняются, а после импорта обновляются таблицы графики и палитра.

src/game_save.cpp добавляет внешний контейнер: идентификатор встроенных игровых данных, метаданные мира/уровня/жизней, полный последний RGB-кадр и проверяемые данные frontend. Absolute simulation frame восстанавливается вместе с состоянием, поэтому повторяемый диагностический ввод продолжается с прежнего шага. Запись проходит через уникальный временный файл, FlushFileBuffers и атомарный MoveFileExW replace/write-through; ошибка записи сохраняет прежний слот. Проверка всех частей и выделение памяти заканчиваются до импорта. Повреждённое сохранение оставляет текущую игру и кадр прежними.

Музыкальные данные ядра и текущие значения APU-регистров восстанавливаются; фаза звукового синтезатора после загрузки начинается заново. Это ограничение звука не меняет положение, движение, врагов, таймер или жизни.

Диагностический CLI: `--save-game PATH` сохраняет состояние при завершении заданного запуска, `--load-game PATH` загружает его до цикла, `--game-save-file PATH` задаёт отдельный слот меню. Эти параметры полезны для воспроизводимых проверок; обычный пользователь работает через меню.

## История: графический этап после коммита 61230ba

src/character_art.* формирует отдельный procedural presentation-кадр 1024×960 по тем же tile/palette callbacks. Светотень и детали меняются только внутри исходной CHR-маски игрока; прежние позы, отражения, палитры и перекрытия сохранены. Нативный raster и Game.pixels 256×240 остаются отдельными контрольными данными. src/game_hud.* рисует read-only score/coins/lives/world/level/timer/currentPlayer; предел HUD — native rows0..31, титульной карточки — x40..215/y32..127 только при mode0/task3. src/glass_panel.* создаёт уменьшенную размываемую копию кадра и кеш SDL-текстуры по revision; фон используется в прозрачных панелях, границах и бликах liquid glass. Исходные RGB и состояние ядра этим кодом не меняются.

Сохранения дополнены presentation RGB с проверяемыми размерами. Поддержаны исходные 48-байтные extra и промежуточный 512×480 кадр; преобразование старого кадра не выделяет память после native import. Дополнительные данные ограничены 4 МиБ. Обычные файлы runtime по-прежнему MarioPC.exe и SDL3.dll; новых внешних assets, шрифтов или сетевых зависимостей нет.

Проверено на окончательном executable: 19 CharacterArtChecks, 191 HudChecks, 50 GlassChecks и 55 graphics frontend; ошибок 0. Хеши RAM/PPU/нативного RGB при300 шагах совпадают с коммитом61230ba; 2288 изменённых raw-пикселей помещаются только в footprint Марио48×64. Новые и оба прежних сохранения дают одинаковое дальнейшее состояние и presentation hash. Повторены42 settings и35 save frontend на этой сборке, включая перенос без исходников с кириллицей/PATH=System32. Все итоговые превью просмотрены. GlassChecks проверил реальное размытие/SDL texture, исходный буфер, clipping и кеш10000 повторов; среднее обновление в отдельном synthetic тесте3.87108мс. Actual paced window:240 кадров со звуком за3994.24мс, около60.09fps; это короткий smoke test. Подробности и provenance: docs/graphics-stage/README.md и JSON/log отчёты рядом.
## Совместимость русского перевода

Проведён статический анализ данного файла; оригинальный английский ROM не скачивался.

- Сравнены 52 участка по 16 байт с контрольными CRC32 из [game-cycle.toml](https://github.com/mstan/SuperMarioBrosNESRecomp/blob/master/game-cycle.toml): 52 совпадения, 0 расхождений. Это сильная выборочная проверка известных участков игрового кода, а не полное сравнение PRG.
- GameText остаётся по адресу 0x8752, LuigiName — 0x87ED, WarpZoneNumbers — 0x87F2, GameTextOffsets — 0x87FE. WriteGameText читает эти таблицы непосредственно из предоставленных данных.
- Таблица GameTextOffsets: 00 00 27 27 46 4E 59 61 6E 6E. Индексы жизней, мира, уровня, имён и warp-зоны согласуются с WriteGameText в C-ядре.
- HUD-координаты и длины в OutputNumbers совпадают с таблицей ROM по адресу 0x8EF4.
- Пакеты титульного экрана в CHR от 0x1EC0 структурно корректны: 28 пакетов, завершение на байте 304 внутри лимита 314 байт ScreenRoutines/vram_buffer.
- Сообщения финала читаются через VRAM_AddrTable_Low/High из ROM. Необычный нулевой заголовок у VRAM-указателя 16 сохранён как свойство переводной версии.

Дополнительные патчи русских надписей или графики по результатам этого анализа не требовались.

## Проверки и ограничения

Отчёт docs/verification/native-checks.json содержит 209 проверок, 0 ошибок. Он проверяет интеграцию production C-ядра с растровым рендером, старт всех 32 обычных уровней, детерминированные повторы и основные механики: движение, ускорение, прыжок и гравитацию, огненный шар, гибель/возрождение, вход в подземелье, переход к следующему уровню, паузу и звуковые последовательности. Огненное усиление и стадия завершения уровня устанавливаются тестовыми fixtures. Это не полное игровое прохождение.

Новая модель настроек прошла 100 assertions SettingsChecks: UTF-8/Unicode roundtrip, повреждённые значения, reserved bindings, атомарная замена и отказ записи, виртуальные кнопки/триггеры/стик, реальные SDL add/remove/reconnect и применение/возврат видео. Настоящее меню прошло 402 MenuChecks: нажатия реальных ImGui-кнопок, назначение и обмен клавиш, Apply/save-and-close, preview без преждевременной записи, отмена и реальный тайм-аут 15 секунд, захват триггера и отключение виртуального геймпада. Готовый executable прошёл 42 проверки tests/frontend_settings_checks.ps1 (docs/frontend-settings/frontend-settings-checks.json): три вкладки с реальной ImGui geometry и PNG полного renderer, 0 core ticks и неизменный RAM hash под меню, custom resolution 960×640, filter/scaling/deadzone/bindings, отдельный процесс повторной загрузки и source-free перенос в кириллическую папку с ограниченным PATH. Скриншоты просмотрены; неподдерживаемые символы стрелок заменены русскими словами. Показанные окна без рамки и exclusive fullscreen подтвердили реальные window/renderer размеры 2560×1440 и actual_fullscreen=true; exclusive actual_mode=2560×1440 (docs/video-settings/visible-mode-1.json, visible-mode-2.json). Проверено на текущем мониторе; физический геймпад не использовался.

SaveChecks прошёл 46 проверок (docs/game-save/save-model-checks.json): точное восстановление и 150 дальнейших кадров в отдельных процессах при беге, прыжке и в подземелье, callbacks нового процесса, повреждённые/чужие/обрезанные файлы без мутации, валидатор frontend и сохранность прежнего слота при заблокированном назначении. tests/save_frontend_checks.ps1 прошёл 35/35 на окончательном executable (docs/game-save/save-frontend-checks.json): 220 + save/load + 80 шагов полностью совпадают с непрерывными 300 по RAM/PPU/RGB/абсолютному шагу; повреждённый файл даёт контролируемый отказ, меню имеет 0 core ticks, перенос без исходников с PATH=System32 даёт тот же результат. Новые действия меню проверены мышью, клавиатурой и реальным виртуальным SDL-геймпадом; подтверждение возврата первоначально выбирает отмену (docs/game-save/menu-checks-result.json). Все четыре страницы меню просмотрены. Физический геймпад не подключался.

Frontend smoke-проверка запускает настоящий executable, встроенные данные, SDL-рендер и звук; отчёт сохраняется в docs/frontend-smoke.json. Проверка обычного темпа с реальным окном и звуком: 240 кадров за 3994.09 мс, около 60.09 кадра/с в этом коротком запуске (docs/game-save/paced-240.json). Перенесённый executable с SDL3.dll в папке с пробелами и кириллицей прошёл запуск с ограниченным PATH без исходников и внешнего ROM (docs/portable-run.json); контрольные хеши RAM и изображения совпали с обычным запуском. При недоступном аудиодрайвере игра продолжает работать без звука, exit code 0 (docs/audio-fallback.json). Это smoke-проверки, а не длительный замер производительности. Превью находятся в docs/verification/native-gameplay-preview.png и docs/verification/all-32-levels.png.

Частота симуляции — 60.0988138974405 шага/с. Тайминг процессора NES по тактам, аппаратные lag frames и точное расписание операций APU не воспроизводятся. Эти ограничения наследуются от native-архитектуры; [описание upstream](https://github.com/nukep/SMB-Vanilla/blob/20f4e02614412f9dbd407dd4e4fab4ade5eb79da/docs/differences.md) также перечисляет ограничения запредельных миров. Проверки не доказывают побитовое совпадение изображения или звука с реальной NES. Полное ручное прохождение, все секреты и глитчи не проверены.

## Пересборка

Проверена сборка Visual Studio 2026 с C++ и Windows SDK, CMake 4.3.3, с сохранёнными dependencies/SDL3-3.4.18, dependencies/imgui-1.92.9b и dependencies/NotoSans. CMakeLists.txt декларирует минимальную версию 3.24, но автоматический build.ps1 использует генератор Visual Studio 18 2026, который должен поддерживаться установленной версией CMake. Встроенные данные и исходники уже присутствуют; скачивание ROM или Python для самой игры не требуется.

```powershell
./build.ps1
# Ручные эквивалентные команды:
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Генератор Visual Studio 17 2022 может использоваться вручную в отдельной папке сборки, но здесь такая сборка не проверялась. Скрипт tools/embed_rom.py предназначен только для разработчика: валидирует пользовательский NROM и атомарно создаёт generated/game_data.h. Его 15 проверок прошли; docs/embedding-tool-checks.json подтверждает побайтовое восстановление предоставленного ROM и неизменность исходного файла. Python нужен только для повторного извлечения данных этим инструментом, а не для игры или пересборки с уже сохранённым заголовком.

Для переноса runtime достаточно MarioPC.exe и SDL3.dll из корня проекта. Для работы с исходниками и повторной сборки сохраняйте остальные папки; полный комплект включается в MarioPC-Windows.zip. docs/delivery-checks.json фиксирует SHA-256 готового executable и переносимый запуск. Физический геймпад в этой проверке не подключался. Лицензии и сведения об авторах находятся в licenses/README.md и оригинальных заголовках исходников.
Исторический аниме-чиби: src/chibi_art.* строил 26 исходных поз в64x128RGBA с4x сглаживанием кривых. CharacterRenderer получает полный снимок8OAM-спрайтов доSMB_tick, распознаёт позу и рисует8 частей через прежние callbacks. Лицо, конечности, рост и смерть теперь имеют новый плавный силуэт; native raster, игровое ядро и pc_settings не изменялись. Все7CTest(1103 проверки) и55frontend прошли; docs/anime-character/verification.json.

Закрытый HUD: исправление ввода — `SettingsMenu::render` отключает ImGui-навигацию клавиатуры и геймпада при закрытом меню; `drawHud` использует `NoNav`. Игровое движение и A больше не выбирают и не активируют кнопку меню. Начальный фокус открытого меню явно показывает курсор навигации, чтобы первое A / Enter активировало пункт. Игровые привязки и ядро не изменены. 571 проверка меню (170 регрессии внутри), Settings 100, HUD 256 и frontend 55 прошли после исправления; docs/anime-character/gamepad-fix.json.

## История: процедурный motion и анатомия

ChibiMotion строит цельный rig: IK конечностей, разные кисти, противофаза и периодическая траектория стоп. PlayerAnimation читает семь полей уже сохраняемой RAM до SMB_tick; phase квантована на24 и не требует истории. Control продвигается после выбора OAM, поэтому reload-таймер считается последним кадром прежней позы. Водная нижняя фаза разделяет elapsed верхнего гребка; changingSize не запускает swim. ThrowTimer управляет восстановлением руки, включая нулевой хвост OAM. Atlas160 имеет LRU. 82 CharacterArt и85frontend прошли; ранние +1/+3/+9 послеsnapshot также совпадают.

## История: импорт мультяшного персонажа из PNG

В предыдущем импорте `sheet_art.*` заменил production-вызов процедурного рисунка; `chibi_art.*` сохранён для исторических экспортов. Native OAM/timing/cache сохраняют прежние функции.43 исходных кадра ×4 варианта64×128 встроены через RLE; подготовка прозрачных PNG выполняет premultiplied Lanczos с постоянным масштабом и общим нижним якорем. Плавание/лазание/бросок/смерть используют разрешённые цельные копии.50 файлов ядра, ввода, меню и сохранений совпали побайтно с версией до импорта. Проверки114/0 и frontend76/0,300 paced около60.08FPS; переносимый EXE+DLL дал те же RAM/PPU/native/presentation хэши. Исходники, манифест и результаты в `docs/sheet-character`.

## Animated scenery

WorldBackground renders1024x960 RGB at12Hz plus per-native-pixel parallax; animation uses the existing saved pre-tick counter. Exact PPU decoration quartets are inspected after NMI, before tiles; scroll was captured before tick. Shared CHR IDs never decide suppression alone. World-only water fill is removed while surface rows remain native. Native HUD, interactive foreground and OAM are retained. Tests38/114/84 allpass, pure native state/framebuffer and save/pause continuation exact.55 core/input/menu/save/character source files unchanged; main additions are visual read snapshots and report fields (docs/world-background/main-visual-integration.diff).


## Bidirectional PC scrolling

Optional compiled-native history caches visited terrain and signed reverse camera movement, keeping original motion rules and forward-only RAM/PPU/RGB. History323696bytes is appended to frontendextra; native snapshot v1 remains90164bytes. Native-aware extra validation runs before mutation. Legacy saves seed only their known visible region; new areas collect full history. Native offscreen enemy despawn remains. Validation39core+35frontend+8existingCTest,62columns exact,portable470frames~60.09FPS; docs/backtracking.


## Real wider world and supplied environment assets

The 256–512 pixel viewport displays real area-parser columns at a fixed 4× scale. BG_WORLD callbacks include absolute metatile metadata. Preview parsing runs in an isolated heap clone and never copies RAM or PPU side effects into the live engine. Visible enemies use extended coordinates; absolute bounding boxes and sprite origins prevent 8-bit collision and sprite aliases. The classic path remains byte exact.

SMB_presentation_ram exposes read-only inputs captured before NMI, so changing the viewport does not advance the simulation. Game.setViewport preserves the existing image overlap and the native control framebuffer, including paused loads between aspect ratios. The native snapshot and BKT history formats remain unchanged. The frontend image extra accepts 1024–2048 × 960 pixels within an 8 MiB limit.

The environment pipeline prepares five MP4 files and four JPG files supplied by the user, records their hashes and PNG checks, and embeds 154 PNG frames in generated/world_asset_data.h. world_assets.cpp decodes PNG from memory through SDL3 and bounds retained RGBA data to 24 frames and 8 MiB. Offscreen objects are skipped before decoding. Decode failures use procedural scenery. Source animations play at 10 FPS with ping-pong loops and three shared phases, using the saved game counter and the existing 12 FPS scenery samples. Clouds, mountains and plants retain world parallax. Lookup tables remove divisions and tint multiplication from the inner image loop.

Runtime needs only the EXE and SDL3.dll. During this environment and wide-view stage, the earlier cartoon character sources were preserved and the presentation renderer changed for viewport dimensions and overlap restoration. Those stage reports and screenshots are in docs/wide-view and docs/world-assets; the current realistic character integration is described below.

## Реалистичный персонаж: текущий рендер

`src/sheet_art.*` использует `generated/realistic_character.h` вместо мультяшного `generated/character_sheets.h`. Два присланных MP4 и один GIF сохранены в `assets/realistic-character/original`; `tools/build_realistic_character.py` создаёт прозрачные подготовленные PNG, маски материалов и встроенный заголовок. Порядок кадров: 14 ходьбы, 21 бега, 26 прыжка. Четыре варианта (большой, маленький, приседание, промежуточный рост) дают 244 атласа 64 × 128 RGBA. RLE-пакеты изображения содержат countLE16/R/G/B/A; отдельные пакеты маски — countLE16/label. Label 0 защищает пиксель, 1/2/3 обозначают шапку, рубашку и комбинезон.

Исходные позы используют общую калибровку размера головы и масштаба, прозрачные края и опорную привязку. Ходьба и бег сохраняют естественное небольшое покачивание; из прыжка удалена записанная в видео вертикальная траектория, поскольку положение тела вычисляет игра. Исходные кадры прыжка 18–21 с обрезанной шапкой исключены; source 17 даёт целую позу вершины, source 29 — первый контакт. Самостоятельной анимации амортизации и возврата в стойку в присланном прыжке нет.

`PlayerAnimation` остаётся read-only снимком. При активной сцене ground gait использует уже сохраняемый `WorldScene.frame`: период бега — 21 игровой шаг, ходьбы — 28, фаза `(frame % period + 0.25)/period`. Без активной сцены сохраняется fallback duration/timer/control; фактическая абсолютная X-скорость от 28 нативных единиц выбирает отдельный цикл бега. Новая частота меняет только смену изображений и не добавляет отдельное состояние аниматора. Y-скорость берётся как signed RAM[0x9F] + unsigned RAM[0x433]/256. Подъём, вершина и падение выбирают позу, не меняя гравитацию или момент нажатия прыжка. Grounded-state с предыдущим jump OAM показывает позу первого контакта без собственного несохраняемого таймера. Приседание, бросок огня, плавание, лазание, рост и гибель переиспользуют цельные позы новых анимаций.

Ключ LRU-кеша из 160 атласов содержит реальный sheet/frame, нативную размерную позу, цвета и флаги формы; одинаковая фаза ходьбы и бега не смешивает кадры. Маски меняют материалы одежды для Луиджи, огненных форм и нативных палитр звезды; защищённые материалы и альфа сохраняются. Рендер по-прежнему делит цельный атлас на прежние восемь OAM-ячеек, сохраняя отражение, скрытие, порядок и перекрытие игровыми объектами.

Игровое C-ядро, обработка клавиатуры/геймпада, настройки и формат сохранений в этом этапе не изменяются. Широкий обзор 256–512 нативных пикселей, история возврата назад, интерфейс и встроенные ресурсы окружения сохраняются. Runtime требует только `MarioPC.exe` и `SDL3.dll`; исходные видео, подготовленные картинки, Python и видеокодек нужны лишь для повторной подготовки ресурсов разработчиком.

Манифест происхождения и подготовленных кадров: [asset-manifest.json](realistic-character/asset-manifest.json). Контактные листы: [ходьба](realistic-character/walk-big.png), [бег](realistic-character/run-big.png), [прыжок](realistic-character/jump-big.png). Проверки actual EXE: [realistic-frontend-checks.json](realistic-character/frontend/realistic-frontend-checks.json). Документы `anime-character`, `animation-polish` и `sheet-character` описывают предыдущие графические этапы и их тогдашние проверки.

Historical realistic validation before tree/coin/fire repairs: 10/10 CTest groups, 1517 assertions; actual frontend103/0; source-free portable11/0 at2560x1440, 59.40FPS, workP95 14.06ms. Native/input/save source hashes26/26 unchanged. Historical EXE SHA256 c3e20fb910c8c017492d8d6d6fa9a4a5734956672362ec0e0272db48061d4475. Evidence docs/realistic-character/verification.json.

## Coins and flower outfit repair (2026-10-14)

Embedded coin RLE data is prepared by tools/build_coin_assets.py from the preserved project original Bitcoin.gif. Background 64x64 and jumping-sprite32x64 atlases retain circular silhouettes and the supplied30frame/100ms sequence. Background replacement requires exact native metatile quads plus palette/nametable or world-column ownership. Jumping sprites require the native coin Misc owner and a complete valid OAM pair; scores, hammers, enemies, used/question blocks and fonts retain native rendering. The HUD uploads one texture only when the saved simulation-derived frame changes. Restoring a saved presentation now restores scene metadata without ticking or redrawing its stored image.

Realistic character material classification follows cap/shirt/denim colors, smooths source GIF dithering for classification only, and downsamples alpha-weighted material coverage. Original244RGBA atlases and their geometry remain byte exact. Neutral facial details and warm skin boundaries remain protected. A read-only StarInvincibleTimer sample distinguishes the native flower palette transition from a real star; non-star fire uses one canonical outfit and a separate cache key. renderSheetFrame and renderSheetMotion share the exact tint path.

Final release evidence: docs/coins/verification.json, docs/fire-form/verification.json, both frontend reports, both offline rebuild proofs, and source-free portable600frame measurement. All12latest CTest groups pass; one old world-render fixture was updated to allow independently verified native collectible coins. No gameplay/input/save-format/background source changes were made in this stage. The inherited first-frame bouncing-block load discrepancy is documented in the coin frontend report.

## Supplied growth and extra-life mushrooms (2026-10-14)

tools/build_mushroom_assets.py preserves both user originals and embeds30frames ×100ms per kind in generated/mushroom_asset_data.h. Fixed64×64 transforms and a common foot row61 avoid independent per-frame fitting. Magenta matte, red floor shadow and reflected contour spill are removed without altering source files. Offline129-file rebuild is byte exact. src/mushroom_art decodes lazily and selects frames from the existing saved simulation clock.

CharacterRenderer.preparePowerups reads actor5 owner/ID/state/type/attributes and a complete native quartet76..79 with redpalette6 or greenpalette5. It accepts native emergence6..11, walking80 and fallingC0; clipped OAMrows/columns keep their remaining original slices. Flowers, stars, poison mushrooms, actor glyphs and title fonts retain their original routes. A strictly qualified cleared-actor collection marker (control6/B,timer2F,exactquartet) preserves one last latched image before OAMhides it; score popup glyphs remain native. No presentation history or new save chunks are added.

The unchanged native OAMlatchedbeforeNMI ordering and callback compositing preserve block/pipe occlusion and viewport clipping. Main only adds the pre-tick preparePowerups call; CMakeadds the embedded asset library and meaningful tests. Current proof: docs/mushrooms/verification.json, all13latest CTest groups, actual SDLfrontend446checks, source-free600frames on2560×1440, and44unchanged protected gameplay/input/save/existing-art source hashes. Gameplay lifecycle is tested with an explicitly assisted isolated pre-emergence item state after a natural1-1run; the core itself performs movement, collection, growth and life reward.

## Frame and audio synchronization (2026-10-14)

NativeFrameClock explicitly returns zero before its deadline, keeps the original 60.0988138974405 Hz simulation rate, bounds catch-up to eight native steps and rebases after an excessive stall. Main queries actual SDL renderer VSync: the software limiter runs only when presentation is unsynchronized. Lifecycle changes reset both scheduling phases. Texture uploads follow presentation revision. Ordinary focus loss suspends the native loop and audio; --autoplay deliberately ignores focus only for unattended diagnostic measurements. --fast/--smoke retain their fixed-step diagnostic semantics, so real pacing measurements use normal --autoplay runs.

SMB_audio_set_paused suspends only playback, preserving queued samples and the synth. Register writes during reset/load remain valid while the menu is open. SMB_audio_reset clears the stream and counters without resuming a paused device. The stream lock makes queue inspection/overflow recovery/append atomic; queued plus incoming samples cannot exceed 60 ms. Old queued sound is discarded before current sound. Telemetry explicitly reports SDL INPUT queue duration, not physical output latency.

Reports now separate native steps, active game time, actual refresh/VSync, present duration, warm presentation intervals, software waits, discarded simulation debt, upload count and audio queue statistics. The legacy double_wait_count placeholder alone is not evidence: the VSync claim is verified by actual_vsync=true together with software_wait_calls=0. Paused presentations deliberately reset interval sampling; pause checks require zero native steps and frozen hashes, rather than active timing samples.

All 15 CTest groups passed (1827 assertions), including 85 pure clock tests across simulated 24–240 Hz and 61 real SDL dummy audio tests. Four real paced SDL cases at the existing 59.951 Hz hardware mode match the prior EXE at equal native steps, including full serialized state and native/presentation pixels. Fresh-process pause/load/continuation is exact. The source-free Cyrillic-path copy contains only EXE and DLL. All 53 protected native core, input/save and artwork files retain their previous hashes. docs/sync/verification.json is the current evidence; prior visual receipts remain historical. The old delivered EXE, DLL and full ZIP are retained in build/before-sync-20261014. No physical high-refresh display or acoustic latency test is claimed.

## Supplied Fireflower animation (2026-10-14)

tools/build_flower_assets.py preserves the original Fireflower.gif byte for byte, keys the magenta matte/floor/shadow, decontaminates contour spill and retains detached warm flame tongues. A fixed square crop (-7 vertical padding) and constant scale preserve source proportions, motion and solid stem row61. All30 original100ms samples are embedded as64x64RGBA RLE in generated/flower_asset_data.h. An independent offline68-file rebuild is exact; source-clipped flame tips in frames5/20 remain faithful to the original.

src/flower_art decodes lazily and selects GIF samples from the existing saved scene counter. CharacterRenderer.preparePowerups also proves native flower ownership actor5/ID2e/type1/state6..11,80,c0. Native tile quartet d6,d6,d9,d9 has cycling upper palette and mirrored right halves; the RGBA already contains complete halves, so exact native flips are validated without mirroring those pixels again. Bottom palette1, owner attribute0/20, positions and visible/hidden OAM slices are checked. Native score marker6/timer2f authorizes exactly one already latched pickup frame after actor erasure, preserving its priority from OAM. Fonts, stars, poison and unrelated sprites keep their prior rendering paths. Native16x16 slices preserve block occlusion and clipping.

All16 CTest groups/1877 assertions passed, including FlowerArt50 and the existing FrameClock85/AudioSync61. Actual SDL tests first produce big Mario by native red mushroom collision from a natural1-1 seed, then initialize only the isolated pre-emergence flower item. Real native movement, collection, score and Fire Mario conversion remain byte exact against the delivered pre-flower EXE. Every native palette phase, all30 supplied GIF phases, final pickup latch, classic256/wide427, pause and fresh-process continuation are tested. No core, main loop, input/save, audio/sync or other graphic asset changed;57 protected source hashes match. Current proof is docs/flower/verification.json. The source-free two-file runtime was measured using normal --autoplay with VSync at2560x1440/59.951Hz, rather than forced-step smoke. Previous EXE/DLL/fullZIP are retained in build/before-flower-20261014.

## Single supplied camel/beetle enemy (2026-10-14)

Only Goomba/ID6 is authorized for substitution. tools/build_camel_enemy_assets.py keeps the supplied MP4 byte-exact, extracts chronological samples82..109, keys matte/floor/reflection, and creates immutable64x96RGBA atlases with blank top32 rows and native16x16 visible body. All28 source poses and one squashed pose are embedded in generated/camel_enemy_asset_data.h; independent offline71-file rebuild is exact. No media decoder is needed at runtime. The crop(276,6,970,700) and62px constant isotropic fit preserve proportions and the floor anchor.

src/camel_enemy_art validates active Goomba owner0..5/ID6, complete native sixcell OAM tile family, positions, flags and palettes. CharacterRenderer.prepareCamelEnemy proves cells once per native draw; the existing callback retains clipping, ordering and block occlusion. Native horizontal waddle bits must not turn the supplied side profile; direction comes from native moving direction, with native Xspeed fallback. Exact last latched Goomba cells survive native erasure only until the next native clear. Unrelated cells stay on their prior paths.

The source rate24 is distinct from cosmetic sample cadence115 at native speed magnitude8; hardmode magnitude12 scales it by3/2. Foot-ground regression R²0.99995 gives about0.26% residual speed mismatch for the measured planted hoof. Frame selection remains on the saved scene clock/native60.0988Hz, with bounded arithmetic, no gameplay write or additional save state. Zero speed and inverted defeated bodies hold source pose0; stomp uses static same-creature squash. During short original internal TimerControl freezes with nonzero Xspeed, cosmetic legs continue as original waddle did; external pause is exact.

All17 CTest groups/1995 assertions passed, including CamelEnemy118. Actual SDL frontend 1874/0 compares baseline native RAM/PPU/pixels/snapshots, all28 source phases, normal/hard/stopped travel, native wallturn, actual stomp/contact, unrelated enemy full images, classic256/wide427 and pause/fresh-process+1/+80. Actor-only assisted native seeds are identified in frontend/assisted-fixtures.json; the core performs events. All59 protected pre-existing sources retain hashes. Source-free twofile runtime checked with normal paced600 presentations at2560x1440/VSync1, 59.951Hz, 58.86pps. The paced-only fixture changes Goomba native Xspeed from-8 to-2 to keep it animated and alive on the starting platform away from stationary Mario; live gameplay, original player/lives and exact loaded fixture hash are checked. Normal8/hard12 movement remains in the real game and is independently verified. No physical controller test was performed. Current proof: docs/camel-enemy/verification.json. Previous EXE/DLL/fullZIP: build/before-camel-20261014.

## Stuck final-stair group and ground support (2026-10-14)

common.c HandleGroupEnemies previously anchored group tokens to smb_viewport_right. A valid input-only route widening256→427/512 while token1b37 remains queued reproduces two Goombas inside finalstair MT61. The fix captures EnemyObjectPageLoc and EnemyData[EnemyDataOffset]&f0 before allocating slots in activewide mode; classic and SMB2 retain the original screen edge. Authored1-1 coordinates2832/2856 lie before staircase2896. Independent pre-fix compilation fails8/25 new tests, fixed25/25 passes. Classic route CSV/full native snapshot/history/RAM remain byte-exact. No position writes, terrain edits or RAM fixtures are used in this reproduction.

WorldScene adds a transient512-pixel support mask, not a save chunk. Game::palette proves y208 floor from exact ground quartet b4,b6,b5,b7/palette1, using currentSMB_viewport_cell and classicPPU fallback. Main defers beginFrame backdrop until this native callback. refreshBackground updates only backdrop/pixels and retains all prepared character/item/camel ownership proofs; ordinarydraw refreshes once. WorldBackground caches validity/mask, binds near plants1:1 to camera, rejects unsupported root footprints, and computes contact from actual nearest resampling at row831. Mountains/clouds retain formerparallax; legacy standalone test scenes without explicit proof retain old composition. Of the translated edge tests, three practicallytransparent pixels allow≤1RGB quantization; opaque roots remain strict.

Current18 CTest groups/2031 assertions, World49/0, Stair25/0, actualSDL frontend55/0, source-free portable16/0. Paired full native state confirms foliage does not change simulation; paused/fresh-process continuation is exact. Currentfixture native/history files originate from the input-only test; frontend wrappers provide a blank prior RGB image and execute actualnative ticks before capturing. Protected59source/asset/control/save/audio/frameclock hashes match. Normalpaced600presentations2560x1440/VSync1 measured 58.66pps, intervalmean16.681ms/P9517.789ms at59.951Hz. The paced-only native actor fixture uses speed2 to remain animated/visible away from stationaryMario; production8/hard12 was verified separately. No physical gamepad test was performed. Previousruntime/fullZIP/sources are in build/before-grounding-fix-20261014. Current proof docs/grounding-fix/verification.json.

## Supplied emerald armadillo (2026-10-14)

Only active standard green Koopa native ID0 is replaced. Source320x180/30frames/10fps, samples17..27 form an11-frame loop. Fixed224-square isotropic crop→62x62 in64x96RGBA, feet row93. Matte removes magenta floor and cast shadow while retaining emerald plates. The static shell is cropped from the same armour; it is provisional until a separate shell clip is supplied. Original plus generator reproduces all38 files byte-exact offline.

Immutable decoded art is proven against six exact native OAM families, owner0..5, active flag1/ID0, coordinates, flags and palette. Other IDs/inactive fallback keep native art, including shared-CHR Piranha and coloured/winged Koopas. Native floating reward digits reuse shell top cells: exact score-pair provenance permits only the four proven body cells to use the replacement; score callbacks stay native. SourceY+8 offsets native shell OAM+2 without altering actor coordinates. Whole-profile mirroring, native clipping and priority remain intact.

Source10fps retimed cosmetically to57 phase samples/s at normal native speed8, based on measured planted-paw motion; cadence scales by abs(native speed)/8. Paused/zero-speed poses remain still, existing saved WorldScene.frame reconstructs phase, no new save chunk. Genuine input drives stomp/kick/revival from labelled actor-only fixtures; no whole-level unaided proof is claimed. Full native core/RAM/PPU/bitmap remains byte-exact to baseline and62 protected source/assets remain exact.

19 CTest groups/2209 assertions, armadillo unit178/0, actualSDL frontend1788/0, portable16/0. Normal paced600 presentations at actual59.951Hz: 58.67pps, mean16.687ms/P9521.743ms. Paced actor-only seed uses test speed2 to stay visible away from stationary Mario; production8/hard12 tested separately. Physical gamepad and high-refresh monitor were not tested. Previous EXE/DLL/archive/source backup: build/before-armadillo-20261014. Current proof: docs/armadillo-enemy/verification.json.

The initial full CTest run had one MarioMenu environmental failure: its virtual-controller harness selected an already-connected physical gamepad. Only that group was rerun with the documented process-local SDL allowlist1209/001c,1209/001b and passed all571 assertions. Product settings and all62 protected files remain byte-exact; both original and isolated logs are retained.
