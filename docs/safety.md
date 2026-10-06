# 安全境界

AI Agentの操作を前提として、対象や入力が不明な場合はfail-closedとします。最寄りActorの推測、active layerの暗黙選択、対象がない場合の自動作成は行いません。

## 実行状態と対象

- Editor game thread上、現在のEditor Worldのみ。
- PIE、Package Save、GC中は編集・読み取りを拒否。
- 完全Object Pathを`FindObject`で解決し、追加Assetロードなし。
- 非World Partition、external actor非使用、可視・ロック解除済みLevel。
- native `ALandscape`のみ。同一Landscape GUIDの追加Proxyも拒否。
- 標準`ULandscapeEditLayer`が1つ、可視、ロック解除、height alpha 1、Blueprint brushなし。
- 無回転、正Scale、Naniteなし、付属Foliageなし、Visibility Holeなし。
- 元HeightmapはLevel Package内部のBGRA8、各軸1024以下。他LandscapeとのTexture共有も拒否。

## 入力と範囲

NaN／Infinityを拒否し、座標は±10000000cmまで。Scaleは[1,1000]。作成16 Component、262144 Sample、各軸1000mまで。既存対象64 Component、262144 Sampleまで。

Radiusは(0,5000]cm、Sculpt Strengthは[0,1000]cm、Smooth／Flatten StrengthとFalloffは[0,1]。編集はhaloを含め16384 Sample以下。中心が外ならFAIL、縁だけならclip。Heightfieldのuint16範囲を超えるSampleが1つでもあれば、書き込み前に編集全体を拒否します。

## 読み取りとDry-run

`FLandscapeEditDataInterface`の通常read pathがTextureをModifyするため、元mipをコピーして読みます。GetHeight／Dry-runはTransactionを開始せず、Package dirtyやUndo履歴を変更しません。Dry-runでも対象Validationと変更計画を省略しません。

## Transaction・Undo・Rollback

MCP facadeは`NonTransactableToolCall`を指定し、操作層自身の`FScopedTransaction`を使用します。新規作成はLevelとActorをModify。高度編集はLandscapeとLandscapeInfoをModifyし、`FScopedSetLandscapeEditingLayer`、`SetHeightData`、`Flush`を用います。

Heightmap Layer更新を要求し、`ForceUpdateLayersContent`を実行。作成は構成・初期高度を照合し、編集は計画済み元Heightfield全体と実データを照合します。照合失敗時は`UndoTransaction(false)`によるrollbackを試み、完了／失敗をエラーへ含めます。rollbackが失敗したら後続編集を停止してください。

UndoはEditor既存コマンドを使用します。書き込みのnested transactionは拒否します。ProgrammaticToolsetなどから外側Transactionで包んだ書き込みは非対応です。

## 保存と副作用

保存／Autosave APIを呼ばず、Autosave設定も変更しません。書き込みによるPackage dirty化はUndoに必要です。Editor独立AutosaveはTool外なので、保存禁止の試験では別途無効にした隔離Editorを使用してください。

付属FoliageやTexture共有など、別Actorへ影響し得る構成を拒否します。Collision更新は実行しますが、結果の歩行可能性を保証するToolではありません。SlopeやCharacter設定の確認が必要です。
