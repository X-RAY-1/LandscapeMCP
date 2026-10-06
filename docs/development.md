# 開発手順と運用

## 言語と構成

README、docs、CHANGELOG、コードコメント、テスト意図のコメント、PRタイトル／本文は日本語を原則とします。C++識別子、UE API、MCP Tool、schema field、ファイル名、Git／GitHub用語は英語のままです。Commit messageは日本語可。

既存実装のエラー文字列・Automation assertion文字列は実行データのため、理由なく変更しません。Testsは`Source/LandscapeMCP/Private/Tests`に置きます。

計算ロジックはMCP facadeから分離します。`LandscapeMCPToolset`（facade）→ `LandscapeMCPOperations`（Validation・対象解決・読み書き）→ `LandscapeMCPTerrainAnalysis`（Landscape・UObject非依存の計算）の順に依存し、逆方向へ依存させません。傾斜・歩行可能性・領域集計の定義は`LandscapeMCPTerrainAnalysis`に1つだけ置き、後続機能から再利用します。

既存Toolの結果構造体へfieldを足すと既存schemaが変わるため、新しいToolには専用の結果構造体を追加します。

Tool追加時はDry-run、Validation、範囲上限、Undo／Transaction、rollback、他Actorへの副作用を検討し、fail-closedを維持してください。安全境界を外す回避実装は行いません。

## Build

UE5.8とWin64 C++ Build環境を用意します。出力先はPlugin repository外の新しいディレクトリを指定します。

```powershell
& '<UE_ROOT>/Engine/Build/BatchFiles/RunUAT.bat' BuildPlugin `
  '-Plugin=<REPO_ROOT>/LandscapeMCP.uplugin' `
  '-Package=<NEW_PACKAGE_DIR>' -TargetPlatforms=Win64 -NoP4
```

パッケージのBinariesを開発ProjectのPluginへ配置してEditorを再起動します。稼働中EditorのPluginを差し替えず、検証用の別Projectを推奨します。生成BinariesはCommitしません。

## Automation

ProjectにBuild済みPluginと公式ToolsetRegistryを有効化し、隔離されたEditorで実行します。Automationは新規未保存Mapを作り、Undo履歴も使用するため、作業中Editorでは実行しないでください。

```powershell
& '<UE_ROOT>/Engine/Binaries/Win64/UnrealEditor.exe' '<TEST_PROJECT>.uproject' `
  /Engine/Maps/Entry -unattended -nop4 -nosplash -NoSound -NoLiveCoding `
  '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorLoadingSavingSettings]:bAutoSaveEnable=False' `
  '-ExecCmds=Automation RunTests LandscapeMCP' `
  '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<REPORT_DIR>'
```

`LandscapeMCP`を指定すると、v0.1の`LandscapeMCP.V01.SafetyAndOperations`、v0.2の`LandscapeMCP.V02.TerrainAnalysis`、v0.3の`LandscapeMCP.V03.TerrainHardening`をすべて実行します。機能追加時は既存suiteも必ず再実行してください。

MCP Serverを併用する場合は作業中Editorとportを分けます。index.jsonの成功数／失敗数と終了コードを確認してください。log・reportはGit管理外で保存します。

## 配布形態

GitHubではソースだけを配布します。Build済みのBinaries（`.dll`、`.pdb`）は、リポジトリにもGitHub Releaseにも置きません。

`RunUAT BuildPlugin`の出力には、`Config/FilterPlugin.ini`の指定により`LICENSE`、`README.md`、`CHANGELOG.md`、`docs`が含まれます。手元でBuildした出力を別の場所へ移すときも、ライセンスと文書が一緒に付いていきます。

Build出力を扱うときの注意:

- 出力の`Intermediate`は、Projectへ配置する必要がありません。
- `.pdb`と`.dll`にはBuild時のローカルパスが埋め込まれます。出力を他人へ渡す場合は、この点に注意してください。

## Git / Release

default branchは`main`です。変更はbranchで行い、PRでレビューしてからMergeします。

Build／Automationの失敗、secretの発見、意図しないコード差分がある場合は、Commit／Pushを止めます。Tool追加や挙動の変更では、既存のAutomation suiteをすべて再実行します。

TagとGitHub Releaseは、PRのMerge後に作成します。GitHub ReleaseにはBuild済みのBinariesを添付しません。
