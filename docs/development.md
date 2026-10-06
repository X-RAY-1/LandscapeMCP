# 開発手順と運用

## 言語・Freeze baseline

README、docs、CHANGELOG、コードコメント、テスト意図のコメント、PRタイトル／本文は日本語を原則とします。C++識別子、UE API、MCP Tool、schema field、ファイル名、Git／GitHub用語は英語のままです。Commit messageは日本語可。

v0.1.0整理ではコメント以外のC++／Build.csトークンとPlugin descriptorをFreeze版と照合し、一致を確認します。既存実装のエラー文字列・Automation assertion文字列は実行データのため変更しません。Testsは`Source/LandscapeMCP/Private/Tests`に保持します。

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
  '-ExecCmds=Automation RunTests LandscapeMCP.V01' `
  '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<REPORT_DIR>'
```

MCP Serverを併用する場合は作業中Editorとportを分けます。index.jsonの成功数／失敗数と終了コードを確認してください。log・reportはGit管理外で保存します。

## Git / Release

default branchは`main`、初版PRは`release/v0.1.0`→`main`。private repositoryで開始します。Build／Automation失敗、secret発見、意図しないコード差分ではCommit／Pushを停止します。

PRはユーザー確認後にMergeします。Merge前に`v0.1.0` Tag／Releaseを作成しません。Licenseの選定も権利者判断まで行いません。
