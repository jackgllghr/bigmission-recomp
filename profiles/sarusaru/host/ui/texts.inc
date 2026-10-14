// Every piece of text the launcher shows, in each of its languages:
// X(id, English, Japanese). {0}, {1}... are filled in by ui::format().
// Included by ui/text.hpp (the IDs) and ui/text.cpp (the tables); the tests
// check that both languages have the same placeholders.

// The main menu
X(Play, "Play", "あそぶ")
X(Settings, "Settings", "オプション")
X(GameDisc, "Game disc", "ゲームディスク")
X(Quit, "Quit", "やめる")
X(FallbackTitle, "SaruSaru Daisakusen", "サルサル大作戦")
X(StatusReady, "Game disc ready", "ゲームディスクの準備OK")
X(StatusChoose, "Choose your game disc to play", "あそぶにはゲームディスクを選んでください")
X(StatusAttention, "Your game disc needs attention", "ゲームディスクを確認してください")
X(ChooseDiscFirst, "Choose your game disc first.", "先にゲームディスクを選んでください。")
X(Disclaimer, "Unofficial fan project. Not affiliated with or endorsed by Sony Interactive Entertainment.",
  "非公式のファンプロジェクトです。株式会社ソニー・インタラクティブエンタテインメントとは関係がなく、承認も受けていません。")

// The game disc
X(DiscTitle, "Game Disc", "ゲームディスク")
X(DiscIntro, "The game runs from your own copy: a disc image (.iso) of the Japanese UMD, {0}.",
  "ゲームはお手持ちのソフトから起動します。日本版UMD（{0}）のディスクイメージを用意してください。")
X(NoDiscChosen, "No disc image chosen yet", "ディスクイメージがまだ選ばれていません")
X(ReadyIn, "Ready to play — {0}", "準備OK — {0}")
X(ChooseBelow, "Choose it below.", "下から選んでください。")
X(FoundIt, "Found it! {0} is ready to play.", "見つかりました！ {0} であそべます。")
X(DiscProblemFor, "{0}: {1}", "{0}：{1}")
X(PlayNow, "Play now", "いますぐあそぶ")
X(FindDisc, "Find the disc image", "ディスクイメージを探す")
X(WorksWithController, "works with a controller", "コントローラーで操作できます")
X(WaitingForPicker, "Waiting for the file picker…", "ファイル選択の画面を待っています…")
X(SystemPicker, "Use the system file picker", "システムのファイル選択を使う")
X(DropHint, "You can also drop the .iso file onto this window.", ".iso ファイルをこのウィンドウにドロップしても選べます。")
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

// The file browser
X(BrowserTitle, "Find your disc", "ディスクを探す")
X(FolderUnreadable, "That folder cannot be opened.", "このフォルダーは開けません。")
X(ShowingAll, "Showing all files", "すべてのファイルを表示")
X(ShowingIso, "Showing .iso only", ".iso のみ表示")
X(UpFolder, "Up a folder", "ひとつ上のフォルダーへ")
X(FolderEmpty, "This folder is empty.", "このフォルダーは空です。")
X(NothingHere, "No folders or disc images here.", "フォルダーもディスクイメージもありません。")
X(PlaceHome, "Home", "ホーム")
X(PlaceDesktop, "Desktop", "デスクトップ")
X(PlaceDownloads, "Downloads", "ダウンロード")
X(PlaceDocuments, "Documents", "書類")
X(PlaceStorage, "Storage", "ストレージ")
X(PlaceComputer, "Computer", "コンピューター")

// Settings
X(SettingsTitle, "Settings", "オプション")
X(TabGraphics, "Graphics", "グラフィック")
X(TabWindow, "Window", "ウィンドウ")
X(TabControls, "Controls", "操作")
X(TabInterface, "Interface", "メニュー")
X(Bindings, "Key and button bindings", "キーとボタンの割り当て")
X(BindingsDetail, "edit settings.ini", "settings.ini で編集")
X(BindingsDescription,
  "Bindings are set in settings.ini for now; this opens its folder. A screen for them is coming.",
  "割り当ては今のところ settings.ini で設定します。ここからそのフォルダーを開きます。専用の画面は今後追加予定です。")
X(SaveFailed, "Settings could not be saved: {0}", "設定を保存できませんでした：{0}")
X(On, "On", "オン")
X(Off, "Off", "オフ")
X(RowResolution, "Resolution", "解像度")
X(DescResolution, "Draws the game at this many times the PSP's 480×272. Higher is sharper and needs a faster GPU.",
  "PSP の 480×272 の何倍で描画するかを選びます。高いほど鮮明になりますが、より高性能な GPU が必要です。")
X(RowInterpolation, "60 FPS motion", "60 FPS 補間")
X(DescInterpolation,
  "Shows a blended picture between the game's own 30 FPS frames, at the original game speed. Adds about 17 ms of delay.",
  "ゲーム本来の 30 FPS のフレームの間に補間した画面を表示します。ゲームの速さは変わりません。遅延が約 17 ms 増えます。")
X(WarnInterpolationRate, "Needs the game update rate at Native; it stays off until then.",
  "ゲーム更新レートが「標準」のときだけ有効です。それまではオフのままです。")
X(WarnInterpolationCap, "Has no effect while the frame rate cap is below 60.",
  "フレームレート上限が 60 未満のあいだは効果がありません。")
X(RowFrameCap, "Frame rate cap", "フレームレート上限")
X(ValueFps, "{0} FPS", "{0} FPS")
X(DescFrameCap, "Limits how often pictures are shown. The game itself keeps its speed.",
  "画面を表示する頻度を制限します。ゲームの速さは変わりません。")
X(RowFov, "Field of view", "視野角")
X(ValueFovNative, "Native (36.5°)", "標準 (36.5°)")
X(DescFov, "Widens the camera in stages and the hub. Menus and cutscenes keep the original view.",
  "ステージとハブでカメラの視野を広げます。メニューやイベントシーンは元の視野のままです。")
X(RowGameRate, "Game update rate", "ゲーム更新レート")
X(ValueGameRateNative, "Native (30)", "標準 (30)")
X(ValueExperimental, "{0} (experimental)", "{0}（実験的）")
X(DescGameRate,
  "Experimental: changes how often the game updates. Movement and animation speed change with it. Use 60 FPS motion "
  "for smooth play at the right speed.",
  "実験的な機能です。ゲームの更新頻度を変えるため、移動やアニメーションの速さも変わります。正しい速さでなめらかに遊ぶには「60 FPS 補間」を使ってください。")
X(RowFullscreen, "Fullscreen", "フルスクリーン")
X(DescFullscreen, "Fills the screen at the desktop's resolution. Alt+Enter or F11 switches while playing.",
  "デスクトップの解像度で画面全体に表示します。プレイ中は Alt+Enter か F11 で切り替えられます。")
X(RowWindowSize, "Window size", "ウィンドウサイズ")
X(DescWindowSize, "The window's size when the game starts. It can still be resized by dragging.",
  "起動時のウィンドウの大きさです。ドラッグして変えることもできます。")
X(RowDeadZone, "Stick dead zone", "スティックのデッドゾーン")
X(DescDeadZone, "How far a stick must move before the game sees it. Raise it if a worn stick drifts.",
  "スティックをどれだけ倒すと反応するかを決めます。スティックが勝手に動く場合は上げてください。")
X(RowConfirm, "Confirm button", "決定ボタン")
X(ValueSouth, "× South", "× 下")
X(ValueEast, "○ East", "○ 右")
X(DescConfirm, "The face button that confirms in these menus. The game itself confirms with ○ (east), as in Japan.",
  "このメニューで決定に使うボタンです。ゲーム本編では日本版と同じく ○（右）で決定します。")
X(RowLanguage, "Language", "言語 (Language)")
X(ValueAutomatic, "Automatic ({0})", "自動（{0}）")
X(DescLanguage, "The language of these menus. Automatic follows the system's language. The game's own text stays Japanese.",
  "このメニューの言語です。「自動」はシステムの言語に合わせます。ゲーム本編の文章は日本語のままです。")

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
X(HintOpen, "Open", "開く")
X(HintOk, "OK", "OK")
