# 開発者向けドキュメント

KT Delay は JUCE を使わず、Steinberg VST 3 SDK と SDK 同梱の VSTGUI だけで書かれています。
GUI は uidesc（XML）を使わず、C++ コードで画像ノブを配置しています。

## 必要なもの

| ツール | バージョン | 用途 |
|---|---|---|
| Visual Studio 2022 | C++ デスクトップ開発ワークロード | コンパイラ |
| CMake | 3.25 以上 | ビルド構成 |
| VST 3 SDK | 3.8.1 で動作確認 | SDK 本体 + VSTGUI |
| Python 3 + Pillow | 任意 | GUI 画像の再生成（`tools/gen_images.py`） |

## VST 3 SDK の配置

SDK はリポジトリに含まれません。<https://www.steinberg.net/developers/> からダウンロードし、
次のどちらかの方法で場所を指定します。

1. **既定の場所に置く**: リポジトリ直下に `VST_SDK/vst3sdk/` となるよう展開する（.gitignore 済み）

   ```
   ktdelay/
     VST_SDK/
       vst3sdk/
         CMakeLists.txt
         base/  pluginterfaces/  public.sdk/  vstgui4/ ...
   ```

2. **別の場所を指す**: CMake 変数か環境変数で `vst3sdk` フォルダを指定する

   ```bat
   cmake -S . -B build -Dvst3sdk_SOURCE_DIR=D:\sdk\VST_SDK\vst3sdk
   rem または
   set VST3_SDK_ROOT=D:\sdk\VST_SDK\vst3sdk
   ```

## ビルド

```bat
build.bat
```

または手動で:

```bat
cmake -G "Visual Studio 17 2022" -A x64 -S . -B build
cmake --build build --config Release --target KTDelay
```

出力は `build\VST3\Release\KTDelay.vst3`（フォルダ形式のバンドル）です。
ビルド後に SDK の `validator` が自動で走り、プラグインを検査します（47 テスト）。

インストールは出力フォルダを丸ごとコピーします。DAW が DLL を掴んでいる間は上書きできないので
DAW を終了してから実行してください。

```bat
xcopy /E /I /Y build\VST3\Release\KTDelay.vst3 "%CommonProgramFiles%\VST3\KTDelay.vst3"
```

Debug ビルドは `build.bat Debug` です。`build\VST3\Debug\` に出力されます。

### CMake オプション

| オプション | 既定 | 説明 |
|---|---|---|
| `vst3sdk_SOURCE_DIR` | `VST_SDK/vst3sdk` | SDK の場所 |
| `KTDELAY_BUILD_TESTS` | OFF | オフライン DSP テストをビルド |
| `SMTG_ENABLE_VST3_HOSTING_EXAMPLES` | OFF | SDK の `editorhost` などをビルド（GUI 単体確認用） |
| `SMTG_CREATE_PLUGIN_LINK` | OFF | ビルド後にシステムの VST3 フォルダへシンボリックリンクを作る（要管理者権限） |

## テスト

### DSP テスト（ホスト不要）

プロセッサを直接駆動し、エコーの位置とレベル、フィードバック減衰、L/R の独立性、
Wet Level、Bypass、Ducking の量と解放、32/64 bit パス、フィードバック時の安定性を数値で検証します。

```bat
cmake -S . -B build -DKTDELAY_BUILD_TESTS=ON
cmake --build build --config Release --target ktdelay_dsptest
build\bin\Release\ktdelay_dsptest.exe
```

### GUI 単体確認

SDK 付属の `editorhost` でエディタだけを開けます。

```bat
cmake -S . -B build -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON
cmake --build build --config Release --target editorhost
build\bin\Release\editorhost.exe build\VST3\Release\KTDelay.vst3
```

## ソース構成

```
CMakeLists.txt              ビルド定義（SDK を add_subdirectory で取り込む）
build.bat                   構成 + ビルド
source/
  ktdelaycids.h             クラス ID、パラメータ ID、既定値
  ktdelayprocessor.*        オーディオ処理
  ktdelaycontroller.*       パラメータ定義、エディタ生成、ホストからの値変更をエディタへ通知
  ktdelayeditor.*           VSTGUI による画像ノブ GUI
  ktdelayentry.cpp          プラグインファクトリ（ベンダー名、URL、連絡先）
  version.h                 バージョン情報（Windows リソースにも使われる）
resource/
  background.png            背景 600 x 260
  knob.png                  ノブのフィルムストリップ 80 x 80 を 101 コマ縦に連結
  bypass.png                Bypass ボタン 96 x 34 を 2 コマ（上 = off、下 = on）
  <UID>_snapshot.png        ホストに表示されるスナップショット
  preview.png               README 用プレビュー
  win32resource.rc          Windows バージョンリソース
tools/gen_images.py         GUI 画像ジェネレータ
tests/dsptest.cpp           オフライン DSP テスト
docs/DEVELOPMENT.md         このファイル
```

## アーキテクチャ

### プロセッサ (`ktdelayprocessor.cpp`)

- L/R 独立のリングバッファ（最大 400 ms + 余裕）。読み出しは線形補間の分数遅延。
- フィードバックは `dry + delayed * feedback` をバッファに書き戻す。100 % は内部ゲイン 0.98 に
  制限して発振を防ぐ。
- パラメータは 1 ポールのローパスで平滑化（ゲイン系 5 ms、ディレイ時間 30 ms）。
  ディレイ時間の変更はテープ的なピッチ変化を伴うグライドになる。
- Ducking: 原音 L/R のピークの大きい方をエンベロープフォロワ（アタック 3 ms、リリース 200 ms）で
  追従し、dB 値を -40 dB → 0、-10 dB → 1 に線形マップした `loudness` を使って
  `wetGain = wet * (1 - ducking * loudness)` とする。
- Bypass は 5 ms のクロスフェードで Wet を 0 にする（原音はそのまま、ディレイラインは動き続ける）。
- 状態は float 4 つ（正規化値）+ int32（bypass）をリトルエンディアンで保存。

### コントローラ (`ktdelaycontroller.cpp`)

- `RangeParameter` で ms / % 表示。Bypass は `kIsBypass` フラグ付き。
- 開いているエディタを登録しておき、`setParamNormalized` が呼ばれたら全エディタの
  `update()` に転送する（ホストのオートメーションやプリセット読み込みを GUI に反映）。

### エディタ (`ktdelayeditor.cpp`)

- `VSTGUIEditor` を継承し、`open()` で `CFrame` に背景、`CAnimKnob` x 4、`COnOffButton`、
  数値表示用 `CTextLabel` をコードで配置する。
- ノブの値変更は `valueChanged()` で `setParamNormalized` + `performEdit`、
  ドラッグ開始/終了で `beginEdit` / `endEdit` をホストへ送る。
- Delay Time の数値表示は `controlBeginEdit` で表示、`controlEndEdit` の 0.9 秒後に
  `CVSTGUITimer` で非表示にする。ホスト側の変更でも一瞬表示する。
- ノブのマウスモードは `getKnobMode()` を上書きし、ホストが相対円形モードを指定した場合以外は
  直線（上下ドラッグ）モードにしている。VSTGUI の既定はカーソル角度追従の円形モード。
- 画像は `KTDelay.vst3/Contents/Resources/` からファイル名で読み込む
  （SDK の Windows バンドルサポートが `CBitmap("knob.png")` を解決する）。

### レイアウト定数

ノブ位置やサイズは `tools/gen_images.py` と `source/ktdelayeditor.cpp` 冒頭の定数で
二重に定義されています。片方を変えたらもう片方も合わせてください。

| 項目 | 値 |
|---|---|
| ウィンドウ | 600 x 260 |
| ノブ | 80 x 80、左端 x = 55 / 195 / 335 / 475、y = 100 |
| ノブのコマ数 | 101 |
| Bypass | 96 x 34、位置 (480, 22) |

## GUI 画像の再生成

```bat
pip install pillow
python tools\gen_images.py
```

`resource/` 以下の PNG がすべて上書きされます。色や形を変えたいときはスクリプト冒頭の定数
（`ACCENT` など）や各 `draw_*` 関数を編集してください。

## CI（GitHub Actions）

`.github/workflows/build.yml` が次を行います。

- `main` への push / pull request: Windows x64 で Release ビルド、SDK validator、DSP テストを実行し、
  バンドルをワークフローの Artifact として保存
- `v*` タグの push: 上記に加えて GitHub Release を作成し、`KTDelay-<tag>-win64.zip` を添付

SDK は Steinberg の公式 GitHub リポジトリ（`steinbergmedia/vst3sdk`）から、
ワークフロー冒頭の `VST3_SDK_REF` で指定したタグをサブモジュール込みで取得します。
SDK を更新するときはこの値とローカルの SDK を合わせてください。

## リリース手順

1. `CMakeLists.txt` の `project(... VERSION x.y.z.w)` を更新する（`projectversion.h` に反映される）。
2. ローカルで `package.bat vX.Y.Z` を実行し、validator と zip の生成を確認する。
3. コミットしてタグを打ち、push する。

   ```bat
   git tag v1.0.0
   git push origin main --tags
   ```

4. Actions の「Build」が完了すると Releases に `KTDelay-v1.0.0-win64.zip` が添付される。
   タグ名に `-` を含む場合（例: `v1.1.0-beta1`）はプレリリースになる。

手動で配布したい場合は `package.bat` が作る `dist\KTDelay-<version>-win64.zip` を
Release に添付してください。zip には `KTDelay.vst3` フォルダ、README、LICENSE が入ります。

## ベンダー情報の変更

ベンダー名、URL、連絡先は `source/ktdelayentry.cpp` の `BEGIN_FACTORY_DEF`、
会社名と著作権表記は `source/version.h` にあります。
