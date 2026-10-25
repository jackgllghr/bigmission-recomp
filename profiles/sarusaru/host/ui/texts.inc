// Every piece of text the launcher shows, in each of its languages:
// X(id, English, Japanese). {0}, {1}... are filled in by ui::format().
// Included by ui/text.hpp (the IDs) and ui/text.cpp (the tables); the tests
// check that both languages have the same placeholders.

// The main menu
X(Play, "Play", "あそぶ")
X(Settings, "Settings", "オプション")
X(Quit, "Quit", "やめる")
X(FallbackTitle, "SaruSaru Daisakusen", "サルサル大作戦")
X(StatusReady, "Game disc ready", "ゲームディスクの準備OK")
X(StatusChoose, "Locate your disc image of {0} to play", "あそぶには {0} のディスクイメージを選んでください")
X(LocateDisc, "Locate game disc", "ゲームディスクを探す")
// The pause menu over the game
X(PausedTitle, "Paused", "ポーズ")
X(Saves, "Saves", "セーブ")
X(StateSlot, "Slot {0}", "スロット {0}")
X(SelectedStateSlot, "Slot {0} · Selected", "スロット {0} ・選択中")
X(EmptySlot, "Empty", "空き")
X(NoThumbnail, "No preview", "画像なし")
X(OpenSavesFolder, "Open saves folder", "セーブフォルダーを開く")
X(OpenSavesFailed, "Could not open saves folder: {0}", "セーブフォルダーを開けません：{0}")
X(ExportSaves, "Export saves", "セーブを書き出す")
X(ImportSavesArchive, "Import .zip", ".zip から読み込む")
X(ImportSavesFolder, "Import folder", "フォルダーから読み込む")
X(SavesExported, "Exported {0} saves.", "{0} 個のセーブを書き出しました。")
X(SavesImported, "Imported {0} saves. They replace this game's saves of the same name.",
  "{0} 個のセーブを読み込みました。同じ名前のセーブは置き換えられました。")
X(SavesNothingFound, "No saves for this game were found there.", "このゲームのセーブが見つかりませんでした。")
X(SavesTransferFailed, "The saves could not be copied: {0}", "セーブをコピーできませんでした：{0}")
X(SavesTransferUnavailable, "The document picker could not open.", "ファイル選択の画面を開けませんでした。")
X(SavesTransferHelp,
  "Game saves are PSP save folders, as on a PSP or in PPSSPP's PSP/SAVEDATA: export them to a .zip, or import a .zip or a folder holding them. Importing replaces saves of the same name.",
  "ゲームのセーブは PSP や PPSSPP の PSP/SAVEDATA と同じ形式のセーブフォルダーです。.zip に書き出すか、それを含む .zip やフォルダーから読み込めます。読み込むと同じ名前のセーブは置き換えられます。")
X(SavesImportInLauncher, "Saves are imported from the launcher, before the game starts.",
  "セーブはゲームを始める前にランチャーから読み込んでください。")
X(StateSlotShortcuts, "Save/load shortcuts use slot {0}.", "ステートの保存・読み込みショートカットはスロット {0} を使います。")
X(SaveState, "Save state", "ステートを保存")
X(LoadState, "Load state", "ステートを読み込む")
X(StateSaved, "State saved", "ステートを保存しました")
X(StateLoaded, "State loaded", "ステートを読み込みました")
X(StateFailed, "Save state error: {0}", "ステートエラー：{0}")
X(Resume, "Resume", "つづける")
X(QuitGame, "Quit game", "ゲームをやめる")
X(HintResume, "Resume", "つづける")
X(AppliesNextStart, "Applies the next time the game starts.", "次にゲームを起動したときに反映されます。")
X(MakeInLauncher, "This choice is made from the launcher's settings; here you can switch between packs already made.",
  "この選択肢はランチャーの設定で作成します。ここでは作成済みのパックだけを切り替えられます。")
X(GameTextInLauncher, "English is downloaded from the launcher's settings first.",
  "英語は先にランチャーの設定からダウンロードしてください。")
X(Disclaimer, "Unofficial fan project. Not affiliated with or endorsed by Sony Interactive Entertainment.",
  "非公式のファンプロジェクトです。株式会社ソニー・インタラクティブエンタテインメントとは関係がなく、承認も受けていません。")

// The game disc
X(DiscProblemFor, "{0}: {1}", "{0}：{1}")
X(WaitingForPicker, "Waiting for the file picker…", "ファイル選択の画面を待っています…")
X(PickerFilter, "PSP disc image (.iso)", "PSP ディスクイメージ (.iso)")

// What is wrong with a chosen disc image
X(DiscMissing, "The file cannot be found.", "ファイルが見つかりません。")
X(DiscUnreadable, "The file cannot be read.", "ファイルを読み込めません。")
X(DiscNotIso, "This file is not a disc image (.iso) that can be read.", "読み込めるディスクイメージ（.iso）ではありません。")
X(DiscNotPsp, "This disc image is not a PSP game.", "PSP のゲームのディスクイメージではありません。")
X(DiscOtherGame, "This is a different PSP game ({0}); the supported disc is {1}.",
  "別の PSP ゲーム（{0}）です。対応しているのは {1} です。")
X(DiscOtherExecutable, "This disc's game program is not the supported version 1.00, or the image is damaged.",
  "対応しているバージョン 1.00 のゲームプログラムではないか、イメージが壊れています。")

// Settings
X(SettingsTitle, "Settings", "オプション")
X(TabGraphics, "Graphics", "グラフィック")
X(TabControls, "Controls", "操作")
X(TabInterface, "Interface", "メニュー")
X(Bindings, "Key and Button Bindings", "キーとボタンの割り当て")
X(BindingsDetail, "keyboard and gamepad", "キーボードとゲームパッド")
X(BindingsDescription, "Choose the keys and gamepad buttons for each PSP control.",
  "PSP の各ボタンに使うキーとゲームパッドのボタンを選びます。")
X(SaveFailed, "Settings could not be saved: {0}", "設定を保存できませんでした：{0}")
X(On, "On", "オン")
X(Off, "Off", "オフ")
X(ValueCelBanded, "Banded", "階調")
X(ValueCelOutlined, "Outlined", "輪郭線付き")
X(DescCelOutlined, "Banded shading with black outlines around characters and the world, like a cartoon. HUD and menus keep their original edges. Experimental: some scenes can look wrong.",
    "段階的な陰影と黒い輪郭線でキャラクターと背景をアニメ風に描画します。HUD とメニューの輪郭はそのままです。実験的な機能のため、一部の場面で表示がおかしくなることがあります。")
X(RowCelShading, "Cel Shading", "セルシェーディング")
X(DescCelShading, "Gives 3D shading distinct bands for a cartoon look. Keeps textures, HUD and menus intact. The cel shading modes are experimental.",
    "3D の陰影を段階的にして、アニメ風に描画します。テクスチャ、HUD、メニューはそのままです。セルシェーディングは実験的な機能です。")
X(RowResolution, "Resolution", "解像度")
X(DescResolution, "Draws the game at this many times the PSP's 480×272. Higher is sharper and needs a faster GPU.",
  "PSP の 480×272 の何倍で描画するかを選びます。高いほど鮮明になりますが、より高性能な GPU が必要です。")
X(RowFrameRate, "Frame Rate", "フレームレート")
X(ValueFps, "{0} FPS", "{0} FPS")
X(ValueFrameRateNative, "Native", "標準")
X(DescFrameRate, "How often pictures are shown. The game itself keeps its speed.",
  "画面を表示する頻度です。ゲームの速さは変わりません。")
X(DescFrameRate60,
  "Shows a blended picture between the game's own 30 FPS frames, at the original game speed. Adds about 17 ms of delay. Experimental: fast motion can show artifacts.",
  "ゲーム本来の 30 FPS のフレームの間に補間した画面を表示します。ゲームの速さは変わりません。遅延が約 17 ms 増えます。実験的な機能のため、速い動きで表示が乱れることがあります。")
X(DescFrameRate120,
  "Shows three blended pictures between the game's own 30 FPS frames, at the original game speed, for 120 Hz displays. Adds about 25 ms of delay. Experimental: fast motion can show artifacts.",
  "ゲーム本来の 30 FPS のフレームの間に補間した画面を3枚表示します（120 Hz のディスプレイ向け）。ゲームの速さは変わりません。遅延が約 25 ms 増えます。実験的な機能のため、速い動きで表示が乱れることがあります。")
X(Experimental, "Experimental", "実験的")
X(RowPreset, "Preset", "プリセット")
X(ValuePresetNative, "Native", "オリジナル")
X(ValuePresetModernLow, "Modern Low", "モダン（軽量）")
X(ValuePresetModern, "Modern", "モダン")
X(ValuePresetCustom, "Custom", "カスタム")
X(DescPresetNative,
  "The game as the PSP drew it: original resolution, textures, field of view and frame rate, Japanese text, native draw distance and aiming, and the intro logos.",
  "PSPと同じ見た目です。元の解像度・テクスチャ・視野角・フレームレート、日本語の文章、標準の描画距離と照準感度で、オープニングのロゴも表示します。")
X(DescPresetModernLow,
  "For phones and other slower devices: 544p rendering, 3× textures, English game text, a 45° field of view, twice the draw distance, the original frame rate, 150% aiming sensitivity and no intro logos. Downloads the textures and English the first time.",
  "スマートフォンなど処理の遅い機器向けの設定です。544pで描画し、3×テクスチャ・英語の文章・視野角45°・2倍の描画距離・元のフレームレート・照準感度150%を使い、オープニングのロゴを飛ばします。はじめて選んだときにテクスチャと英語をダウンロードします。")
X(DescPresetModern,
  "1080p rendering, HD textures, English game text, the farthest draw distance, 150% aiming sensitivity and no intro logos. Downloads the textures and English the first time.",
  "1080pで描画し、HDテクスチャ・英語の文章・最大の描画距離・照準感度150%を使い、オープニングのロゴを飛ばします。はじめて選んだときにテクスチャと英語をダウンロードします。")
X(DescPresetCustom, "Your own mix of the settings below. Choose a preset to set them all at once.",
  "下の設定を個別に選んでいます。プリセットを選ぶと、まとめて設定されます。")
X(RowFov, "Field of View", "視野角")
X(ValueFovNative, "Native (36.5°)", "標準 (36.5°)")
X(DescFov, "Widens the camera in stages and the hub. Menus and cutscenes keep the original view.",
  "ステージとハブでカメラの視野を広げます。メニューやイベントシーンは元の視野のままです。")
X(RowDrawDistance, "Draw Distance", "描画距離")
X(ValueDrawDistanceNative, "Native", "標準")
X(DescDrawDistance,
  "Keeps enemies, monkeys and other characters drawn farther from the camera. Some enemies also start moving from farther away.",
  "敵やサルなどのキャラクターを、カメラから遠くても表示します。一部の敵は遠くから動き出すようになります。")
X(RowFullscreen, "Full Screen", "フルスクリーン")
X(DescFullscreen, "Fills the screen at the desktop's resolution. Alt+Enter or F11 switches while playing.",
  "デスクトップの解像度で画面全体に表示します。プレイ中は Alt+Enter か F11 で切り替えられます。")
X(RowWindowSize, "Window Size", "ウィンドウサイズ")
X(DescWindowSize, "The window's size when the game starts. It can still be resized by dragging.",
  "起動時のウィンドウの大きさです。ドラッグして変えることもできます。")
X(DescWindowSizeFullscreen, "Full Screen is on. Turn it off to choose a window size.",
  "フルスクリーンがオンです。ウィンドウサイズを選ぶにはオフにしてください。")
X(RowAimSensitivity, "Aiming Sensitivity", "照準感度")
X(DescAimSensitivity, "Stick response while aiming a monkey ability. Either stick can aim; 100% uses the original response. Higher values reach full aiming speed sooner.",
    "サルの能力で狙うときのスティック感度。左右どちらでも照準を動かせます。100%が元の感度です。高い値ほど少ない操作で最大速度になります。")
X(RowDeadZone, "Stick Dead Zone", "スティックのデッドゾーン")
X(DescDeadZone, "How far a stick must move before the game sees it. Raise it if a worn stick drifts.",
  "スティックをどれだけ倒すと反応するかを決めます。スティックが勝手に動く場合は上げてください。")
X(RowSkipIntroLogos, "Skip Intro Logos", "起動時のロゴをスキップ")
X(DescSkipIntroLogos, "Skip the opening logos automatically. You can also press the game’s Start button during the logos (Enter by default on keyboard).",
  "起動時のロゴを自動でスキップします。ロゴの表示中にゲームのSTARTボタン（キーボードの初期設定はEnter）を押してもスキップできます。")
X(RowConfirm, "Confirm Button", "決定ボタン")
X(ValueSouth, "South", "下")
X(ValueEast, "East", "右")
X(DescConfirm, "The face button that confirms in these menus. The game itself confirms with the east button (○ on PlayStation), as in Japan.",
  "このメニューで決定に使うボタンです。ゲーム本編では日本版と同じく右のボタン（PlayStationの○）で決定します。")
X(RowLanguage, "Language", "言語 (Language)")
X(ValueAutomatic, "Automatic ({0})", "自動（{0}）")
X(DescLanguage, "The language of these menus. Automatic follows the system's language. Game Text sets the game's own.",
  "このメニューの言語です。「自動」はシステムの言語に合わせます。ゲーム本編の言語は「ゲームの文章」で選びます。")
X(RowGameText, "Game Text", "ゲームの文章")
X(ValueGameJapanese, "Japanese (original)", "日本語（オリジナル）")
X(ValueGameEnglish, "English (unofficial)", "英語（非公式）")
X(DescGameText,
  "The language of the game's own menus, names and tutorials. English is an unofficial machine translation, downloaded the first time it is chosen.",
  "ゲーム本編のメニュー・名前・チュートリアルの言語です。英語は非公式の機械翻訳で、はじめて選んだときにダウンロードします。")
X(RowSubtitles, "Movie Subtitles", "ムービーの字幕")
X(DescSubtitles, "English subtitles over the cutscene movies, for the movies that have subtitles installed.",
  "ムービーシーンに英語の字幕を表示します。字幕が入っているムービーだけに表示されます。")

X(RowTextures, "Textures", "テクスチャ")
X(ValueTexturesOriginal, "Original", "オリジナル")
X(ValueTextures2x, "2×", "2×")
X(ValueTextures3x, "3×", "3×")
X(ValueTexturesHd, "HD (4×)", "HD（4×）")
X(ValueTextures4k, "4K (8×)", "4K（8×）")
X(ValueTexturesCustom, "Custom pack", "カスタムパック")
X(DescTextures,
  "Sharper textures for the menus, icons, backgrounds and the world. Downloaded, or upscaled on this computer from your own disc, the first time each is chosen.",
  "メニュー・アイコン・背景・ステージのテクスチャを高解像度にします。はじめて選んだときにダウンロードするか、お手持ちのディスクからこのコンピューターで作ります。")

// HD textures (made on this computer)
X(TexturesTitle, "HD Textures", "HDテクスチャ")
X(TexturesIntro,
  "The game's textures are upscaled on this computer from your own disc by Real-ESRGAN, an image upscaler. No game art is downloaded: only the list of what to upscale and the upscaler itself. This can take a while, and the game keeps some textures to upscale next time as you play.",
  "ゲームのテクスチャを、画像拡大ツール Real-ESRGAN でお手持ちのディスクからこのコンピューター上で高解像度にします。ゲームの絵はダウンロードしません。ダウンロードするのは対象のリストと拡大ツールだけです。時間がかかることがあります。プレイ中に見つかったテクスチャは、次回にまとめて高解像度にします。")
X(TexturesSource, "Downloads up to {0} MB from {1}.", "{1} から最大 {0} MB をダウンロードします。")
X(TexturesMake, "Make {0} textures", "{0} テクスチャを作る")
X(TexturesUpdate, "Upscale the textures collected while playing", "プレイ中に集めたテクスチャを高解像度にする")
X(TexturesDownloading, "Downloading: {0} of {1} MB", "ダウンロード中：{0} / {1} MB")
X(TexturesUnpacking, "Unpacking the upscaler…", "拡大ツールを展開しています…")
X(TexturesReading, "Reading textures from your disc…", "ディスクからテクスチャを読み込んでいます…")
X(TexturesUpscaling, "Upscaling: {0} of {1} images", "高解像度化中：{0} / {1} 枚")
X(TexturesWriting, "Saving: {0} of {1} textures", "保存中：{0} / {1} テクスチャ")
X(TexturesFailed, "The textures were not made: {0}", "テクスチャを作れませんでした：{0}")
X(TexturesUnavailable, "This build of the port does not offer HD textures.", "このビルドではHDテクスチャを利用できません。")
X(TexturesNoUpscaler, "The upscaler has no build for this computer.", "このコンピューター用の拡大ツールがありません。")
X(TexturesPrebuiltIntro,
  "A ready-made HD pack for the menus, icons, backgrounds and game textures, published apart from the port. It is downloaded once and kept on this computer.",
  "メニュー・アイコン・背景・ゲーム内のテクスチャを高解像度にした既製のHDパックです。移植とは別に公開されていて、一度ダウンロードするとこのコンピューターに保存されます。")
X(TexturesDownload, "Download HD textures", "HDテクスチャをダウンロード")
X(TexturesUnpackingPack, "Unpacking the textures…", "テクスチャを展開しています…")
X(TexturesNotDownloaded, "The textures were not downloaded: {0}", "テクスチャをダウンロードできませんでした：{0}")
X(TexturesNeedDisc, "Choose your game disc first.", "先にゲームディスクを選んでください。")

// The English game text (an optional download)
X(PackTitle, "English Game Text", "英語のゲーム文章")
X(PackIntro,
  "The English text is an unofficial fan translation, made by machine and not yet reviewed. It is not made or endorsed by Sony Interactive Entertainment. It covers the main menus, names and tutorials; the rest stays Japanese.",
  "英語の文章は非公式のファン翻訳で、機械翻訳のため、まだ人の手で確認されていません。ソニー・インタラクティブエンタテインメントが作成・承認したものではありません。おもなメニュー・名前・チュートリアルが英語になり、それ以外は日本語のままです。")
X(PackSource, "It is downloaded from {0} ({1} MB).", "{0} からダウンロードします（{1} MB）。")
X(PackDownload, "Download and use English", "ダウンロードして英語にする")
X(PackDownloading, "Downloading: {0} of {1} MB", "ダウンロード中：{0} / {1} MB")
X(PackFailed, "The download did not finish: {0}", "ダウンロードが完了しませんでした：{0}")
X(PackUnavailable, "This build of the port does not offer the English text.",
  "このビルドでは英語の文章を利用できません。")

// The bindings screen
X(BindingsTitle, "Controls", "操作設定")
X(ColumnKeyboard, "Keyboard", "キーボード")
X(ColumnGamepad, "Gamepad", "ゲームパッド")
X(Unbound, "None", "なし")
X(ResetKeyboard, "Reset keyboard to defaults", "キーボードを初期設定に戻す")
X(ResetGamepad, "Reset gamepad to defaults", "ゲームパッドを初期設定に戻す")
X(DescBindingCell, "Set replaces this input; Add keeps it and adds another; Clear leaves it unbound.",
  "「設定」で入力を置き換え、「追加」でもうひとつ加え、「消去」で割り当てを外します。")
X(DescReset, "Puts every binding for this device back as it was; the other device is kept.",
  "この機器の割り当てをすべて最初の状態に戻します。もう一方の機器はそのままです。")
X(AlsoUsedFor, "Also used for: {0}", "ほかの操作にも使われています：{0}")
X(NotBound, "Not bound: this does nothing until something is set.", "割り当てなし：設定するまで使えません。")
X(CaptureKey, "Press a key for {0}", "{0} に使うキーを押してください")
X(CapturePad, "Press a button, hold two together, or move a stick for {0}", "{0} に使うボタンを1つ、または2つ同時に押すか、スティックを倒してください")
X(CaptureAdding, "It will be added to the others.", "今の割り当てに追加されます。")
X(CaptureReplacing, "It will replace the current input.", "今の割り当てと置き換わります。")
X(CaptureKeyCancel, "Esc cancels. (Esc itself can be set in settings.ini.)",
  "Esc でキャンセルします。（Esc 自体は settings.ini で設定できます）")
X(CaptureHoldCancel, "Hold to cancel", "長押しでキャンセル")
X(CaptureSeconds, "Cancels in {0} s", "あと {0} 秒でキャンセル")
X(CapturePointerCancel, "Click or tap to cancel.", "クリックかタップでキャンセルします。")
X(ActionUp, "\u2191 Up", "\u2191 上")
X(ActionDown, "\u2193 Down", "\u2193 下")
X(ActionLeft, "\u2190 Left", "\u2190 左")
X(ActionRight, "\u2192 Right", "\u2192 右")
X(ActionCircle, "\u25CB Circle", "\u25CB ボタン")
X(ActionCross, "\u00D7 Cross", "\u00D7 ボタン")
X(ActionSquare, "\u25A1 Square", "\u25A1 ボタン")
X(ActionTriangle, "\u25B3 Triangle", "\u25B3 ボタン")
X(ActionL, "L button", "L ボタン")
X(ActionR, "R button", "R ボタン")
X(ActionStart, "START", "START")
X(ActionSelect, "SELECT", "SELECT")
X(ActionAnalogLeft, "Analog stick \u2190", "アナログパッド \u2190")
X(ActionAnalogRight, "Analog stick \u2192", "アナログパッド \u2192")
X(ActionAnalogUp, "Analog stick \u2191", "アナログパッド \u2191")
X(ActionAnalogDown, "Analog stick \u2193", "アナログパッド \u2193")
X(ActionCameraLeft, "Camera ←", "カメラ ←")
X(ActionCameraRight, "Camera →", "カメラ →")
X(ActionCameraUp, "Camera ↑", "カメラ ↑")
X(ActionCameraDown, "Camera ↓", "カメラ ↓")
X(ActionCameraReset, "Reset camera", "カメラをリセット")
X(ActionFastForward, "Fast forward (hold)", "早送り（押している間）")
X(ActionMenu, "Pause menu", "ポーズメニュー")

// Installing the game from the disc
X(InstallTitle, "Install the Game", "ゲームのインストール")
#if defined(__ANDROID__)
// A phone takes longer, and the installation carries on behind other apps
// (SaruSaruActivity's InstallService).
X(InstallIntro,
  "Before the first game, the launcher builds the game for this device from your disc. It takes about ten minutes, carries on if you switch to another app, and is needed again only after an update. Nothing from your disc leaves this device.",
  "はじめてあそぶ前に、ランチャーがお手持ちのディスクからこの端末用にゲームを作ります。10分ほどかかり、ほかのアプリに切り替えても続きます。次に必要になるのはアップデートのあとだけです。ディスクの内容がこの端末の外に送られることはありません。")
#else
X(InstallIntro,
  "Before the first game, the launcher builds the game for this computer from your disc. It takes a few minutes and is needed again only after an update. Nothing from your disc leaves this computer.",
  "はじめてあそぶ前に、ランチャーがお手持ちのディスクからこのコンピューター用にゲームを作ります。数分かかりますが、次に必要になるのはアップデートのあとだけです。ディスクの内容がこのコンピューターの外に送られることはありません。")
#endif
X(InstallOptions,
  "Choose your textures and game text before installing. Your current choices are kept after an update.",
  "インストール前にテクスチャとゲームの言語を選んでください。アップデート後も現在の設定は引き継がれます。")
X(InstallStart, "Install", "インストールする")
X(InstallDownloadNote, "First the compiler is downloaded ({0} MB).", "まずコンパイラーをダウンロードします（{0} MB）。")
X(InstallNeedXcode, "This needs Apple's Command Line Tools, which macOS can install for you.",
  "AppleのCommand Line Toolsが必要です。macOSがインストールできます。")
X(InstallGetXcode, "Install the Command Line Tools", "Command Line Toolsをインストールする")
X(InstallXcodeWaiting, "Finish the installation in the window macOS opened, then check again.",
  "macOSが開いたウィンドウでインストールを終えてから、もう一度確認してください。")
X(InstallNeedCompiler,
  "This needs a C++ compiler. Install g++ from your distribution (for example: sudo apt install g++), then check again.",
  "C++コンパイラーが必要です。お使いのディストリビューションからg++をインストールしてから（例：sudo apt install g++）、もう一度確認してください。")
X(InstallUnsupported, "This build of the port cannot install the game.", "このビルドではゲームをインストールできません。")
X(InstallCheckAgain, "Check again", "もう一度確認する")
X(InstallStepStarting, "Getting ready…", "準備しています…")
X(InstallStepPreparing, "Preparing the compiler…", "コンパイラーを準備しています…")
X(InstallStepDownloading, "Downloading the compiler: {0} of {1} MB", "コンパイラーをダウンロード中：{0} / {1} MB")
X(InstallStepExtracting, "Reading your disc…", "ディスクを読み込んでいます…")
X(InstallStepGenerating, "Translating the game's code…", "ゲームのコードを変換しています…")
X(InstallStepCompiling, "Building the game: {0} of {1}", "ゲームを作っています：{0} / {1}")
X(InstallStepLinking, "Putting it together…", "仕上げています…")
X(InstallCancel, "Cancel", "キャンセル")
X(InstallFailed, "The installation did not finish: {0}", "インストールが完了しませんでした：{0}")
X(InstallTryAgain, "Try again", "もう一度ためす")
// The notification while the game installs behind other apps (Android)
X(InstallNotifyTitle, "Installing the game", "ゲームをインストールしています")
X(InstallNotifyDone, "The game is installed. Tap to play.", "ゲームのインストールが完了しました。タップしてあそびましょう。")
X(InstallNotifyFailed, "The installation did not finish. Tap to see why.", "インストールが完了しませんでした。タップして理由を確認してください。")
X(HintCancel, "Cancel", "キャンセル")

// Problems
X(CouldNotStart, "Could not start", "起動できませんでした")
X(CouldNotStartMessage, "The game could not start: {0}", "ゲームを起動できませんでした：{0}")
X(Ok, "OK", "OK")

// Controls and button hints
X(Back, "Back", "もどる")
X(HintSelect, "Select", "決定")
X(HintBack, "Back", "もどる")
X(HintQuit, "Quit", "やめる")
X(HintChange, "Change", "変更")
X(HintTabs, "Tabs", "タブ切替")
X(HintOk, "OK", "OK")
X(HintSet, "Set", "設定")
X(HintAdd, "Add", "追加")
X(HintClear, "Clear", "消去")

X(RowControllerButtons, "Controller Buttons", "ボタン表示")
X(DescControllerButtons, "Choose the button icons shown in menus and in-game. Auto follows the keyboard or controller last used. Bindings stay the same.", "メニューとゲーム内のボタン表示を選びます。自動では最後に使ったキーボードやコントローラーに合わせます。ボタン割り当ては変わりません。")
X(ValuePromptAuto, "Auto", "自動")
X(ValuePromptXbox, "Xbox", "Xbox")
X(ValuePromptSwitch, "Switch", "Switch")
X(ValuePromptPlayStation, "PlayStation", "PlayStation")
X(ValuePromptKeyboard, "Keyboard", "キーボード")

X(TabUpdates, "Updates", "更新")
X(RowAutomaticUpdates, "Check for Updates", "更新を自動確認")
X(DescAutomaticUpdates, "Check for a new release once a day when the launcher opens. You can always check manually.", "ランチャーを開いたときに1日1回、新しいバージョンを確認します。手動でも確認できます。")
X(InstalledVersion, "Installed version: {0}", "現在のバージョン：{0}")
X(UpdateAvailable, "Update available: {0}", "新しいバージョン：{0}")
X(UpdatesCheck, "Check Now", "今すぐ確認")
X(UpdatesChecking, "Checking…", "確認中…")
X(UpdatesCurrent, "You have the latest version.", "最新のバージョンです。")
X(UpdatesUnsupported, "A new release exists, but it has no package for this computer.", "新しいバージョンがありますが、この端末用のパッケージはありません。")
X(UpdatesFailed, "Could not check for updates. Check your connection and try again later.", "更新を確認できませんでした。接続を確認し、後でもう一度お試しください。")
X(UpdatesDownloads, "Open Release Downloads", "リリースページを開く")
X(UpdatesDownloadHelp, "Download the package for your system, then close the game before replacing the launcher. Your saves and settings are kept separately.", "端末用のパッケージをダウンロードし、ゲームを終了してからランチャーを更新してください。セーブデータと設定は別に保存されています。")
X(UpdatesFlatpak, "Update Flatpak through its configured repository, or install a newer bundle from the release page.", "Flatpak版は設定済みのリポジトリから更新するか、リリースページから新しいバンドルをインストールしてください。")
X(UpdatesAndroid, "Obtainium can check GitHub Releases and install Android updates. Add this app to Obtainium to get started.", "ObtainiumでGitHubリリースの確認とAndroid版の更新ができます。このアプリをObtainiumに追加してください。")
X(UpdatesObtainium, "Add to Obtainium", "Obtainiumに追加")

// Game content: the English text and HD texture packs, and rebuilding the game
X(ContentHeading, "Game content", "ゲームデータ")
X(ContentEnglish, "English text: {0}", "英語の文章：{0}")
X(ContentHd, "HD textures: {0}", "HDテクスチャ：{0}")
X(ContentNotInstalled, "not downloaded", "未ダウンロード")
X(ContentCustomPack, "a pack of your own", "独自のパック")
X(ContentEarlierVersion, "an earlier version ({0})", "以前のバージョン（{0}）")
X(ContentVersion, "version {0}", "バージョン {0}")
X(ContentVersionDated, "version {0} ({1})", "バージョン {0}（{1}）")
X(ContentCheck, "Check for Newer Game Content", "新しいゲームデータを確認")
X(DescContentCheck, "Asks GitHub for the newest English text and HD textures. Nothing is downloaded until you choose to install one.",
  "最新の英語の文章とHDテクスチャをGitHubに確認します。インストールを選ぶまで何もダウンロードしません。")
X(DescContentInstall, "Downloads this release and replaces the one installed now.",
  "このリリースをダウンロードして、今のものと置き換えます。")
X(ContentFailed, "Could not check for newer game content. Check your connection and try again later.",
  "新しいゲームデータを確認できませんでした。接続を確認し、後でもう一度お試しください。")
X(ContentEnglishCurrent, "The latest English text is installed.", "最新の英語の文章がインストールされています。")
X(ContentHdCurrent, "The latest HD textures are installed.", "最新のHDテクスチャがインストールされています。")
X(ContentInstallEnglish, "Install English Text {0}", "英語の文章 {0} をインストール")
X(ContentInstallHd, "Install HD Textures {0}", "HDテクスチャ {0} をインストール")
X(ContentInLauncher, "Game content is installed from the launcher's settings.",
  "ゲームデータはランチャーの設定からインストールします。")
X(PackVersion, "Downloads {0}, {1} MB, from {2}.", "{2} から{0}をダウンロードします（{1} MB）。")
X(DescInstalledEnglish, "Installed: {0}.", "インストール済み：{0}。")
X(DescInstalledHd, "Installed: {0}.", "インストール済み：{0}。")
X(PackNeedsNewerPort, "This English text needs a newer version of the port; the one installed before is kept.",
  "この英語の文章には新しいバージョンのポートが必要です。以前の文章をそのまま使います。")
X(RebuildGame, "Rebuild the Game", "ゲームを作り直す")
X(DescRebuild, "If the game misbehaves or will not start, build it again from your disc. It takes as long as installing it did; saves, settings and downloads are kept.",
  "ゲームの動作がおかしいときや起動しないときは、ディスクからもう一度作り直します。インストールと同じくらい時間がかかります。セーブデータ・設定・ダウンロードはそのままです。")
X(RebuildTitle, "Rebuild the Game", "ゲームの再構築")
X(RebuildIntro, "This builds the game from your disc again and replaces the installed copy. Your saves, settings, English text and HD textures are kept.",
  "ディスクからゲームをもう一度作り、インストール済みのゲームと置き換えます。セーブデータ・設定・英語の文章・HDテクスチャはそのままです。")
X(RebuildStart, "Rebuild", "作り直す")
X(RebuildDone, "The game was rebuilt.", "ゲームを作り直しました。")
X(RebuildNeedsDisc, "Choose your game disc first; the game is rebuilt from it.",
  "先にゲームディスクを選んでください。ディスクから作り直します。")
