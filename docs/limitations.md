# 制限

v0.4.0時点の制限です。v0.4で追加した3 Tool（FlattenRectangle / FlattenPath / CreateWalkablePath）に固有の制限は、「v0.4の3 Toolの制限」と[v0.4 Apply contract](v0.4-apply.md)にあります。

## 意図的なFAIL

World Partition、external actor Level、Streaming Proxy、Landscape subclass、同一GUIDの追加Proxy、非表示／ロックLevel、回転Landscape、負Scale、異常Scale、Naniteは拒否します。

Edit Layerは標準Layerが1つだけ必要です。複数Layer、非表示／ロックLayer、height alphaが1以外、Blueprint／procedural brushは非対応です。Visibility Hole、欠損Component、外部／共有／巨大Heightmapも拒否します。通常読み取りでも同じ条件を検証します。

付属Foliage（v0.5）: 読み取りToolとv0.4の3 ToolのDry-runは、Foliageが付いたLandscapeでも使えます。v0.4の3 ToolのApplyは、Foliageへの影響がない場合（`foliage.verdict`が`NO_FOLIAGE`か`SAFE_TO_APPLY`）だけ書き込みます。円形ブラシの3 Toolは、付属Foliageがあれば従来どおり拒否します。

PIE／Save／GC中の操作、nested write transaction、入力・作成・編集Sample上限超過、Heightfield overflowはFAILです。

## 未実装

- path探索（FindWalkablePath）、不可歩行領域の自動Smooth
- 回転した矩形の平坦化（`FlattenRectangle`の`yawDegrees`は0のみ）
- 複数Edit Layerの明示選択
- Heightmap適用Tool
- 独立RebuildCollision Tool
- World Partition／Proxy対応
- Landscape Grass（Grass Output・Grass Type）を使うLandscapeへの編集（下の「Landscape Grass」を参照）
- Water System・Splineの検出・保護・自動追従と、保護領域の設定（下の「Water System・Spline・保護領域」を参照）
- `NEAR_LIMIT`／`slopeUncertaintyDegrees`の名称見直し（schema互換性のため保留）

## 地形評価Toolの制限

`EvaluateWalkability`と`EvaluateWalkabilityRegion`は**Landscape形状のgeometry評価であり、CharacterMovementの完全な再現ではありません**。比較するのは傾斜角（前者は元Heightfieldのbilinear面の中心差分、後者は実三角形ごとの傾斜）と、呼び出し側が渡した`walkableFloorAngleDeg`だけです。

- 特定のCharacter、Blueprint、CharacterMovementComponentを参照しません。Walkable Floor Angleは呼び出し側が調べて渡します。
- CharacterMovementは接触したCollision三角形の法線、Capsuleの形状、Step Height、Perch、速度、Walkable Slope Overrideなどで判定します。本Toolはこれらを扱いません。
- 傾斜は`sampleDistanceCm`の幅で平均した値です。幅より細かい段差や折れ目は平均化され、Collision三角形面との差もあります。
- `WALKABLE`は歩行できることの保証ではなく、`UNWALKABLE`は進入できないことの保証でもありません。判定は実走行で確認してください。
- `NEAR_LIMIT`は、高さの量子化誤差に対する測定上の区分です。Collision三角形との差、Capsule、Step Height、Perch、その他のCharacterMovementの挙動による不確実性は含みません。
- 中心傾斜（`GetSlope`、`EvaluateWalkability`）は対称な地形で打ち消し合います。丘の頂点、尾根、谷底、鞍部では両側が急でも傾斜0・`WALKABLE`になります。これは定義どおりの挙動です。周囲の急な面は`AnalyzeSlopeNeighborhood`の`localMaxSlopeDegrees`、`GetHeightRegion`の`maxTriangleSlopeDegrees`、`EvaluateWalkabilityRegion`で確認してください。
- 局所最大傾斜と`EvaluateWalkabilityRegion`は実三角形1枚ごとの評価です。Characterの足元より小さい急な三角形が1枚あるだけで最大値は大きくなり、領域は`MIXED`や`UNWALKABLE`になります。実際に通れるかどうかはCapsuleの大きさやStep Heightに依存します。
- 実三角形の分割（対角線00-11）はUE5.8で確認したものです。別のEngineバージョンでは、Automationのline trace確認で検出できます。
- 実三角形の評価はCollisionのLODや簡易Collision（Simple Collision）を考慮しません。Collisionの解像度をHeightfieldより粗くしたLandscapeでは、実際の接触面と一致しません。
- 1点の評価です。経路全体の評価は、呼び出し側が複数点または`GetHeightRegion`で行います。

`GetSlope`と`EvaluateWalkability`は、中心から`sampleDistanceCm`離れた4点すべてがLandscape内にない場合FAILします。端では片側差分へ切り替えません。そのためLandscapeの縁から`sampleDistanceCm`未満の帯は計測できません。

`AnalyzeSlopeNeighborhood`と`EvaluateWalkabilityRegion`は1回あたり最大16384頂点です。`EvaluateWalkabilityRegion`は矩形をclipせず、少しでも範囲外ならFAILします。`AnalyzeSlopeNeighborhood`は中心傾斜の4点が範囲外ならFAILしますが、半径だけは端で切ります。

`GetHeightRegion`は1回あたり最大1024 Sampleです。最大のLandscape（262144 Sample）を格子解像度で一度に読むことはできず、領域の分割か間隔の拡大が必要です。矩形はclipされず、少しでも範囲外ならFAILします。`maxSlopeDegrees`は指定間隔のセル単位の値で、間隔より細かい地形は反映しません。

評価Toolはすべて元Heightfieldを読むため、`GetHeight`と同じ対象条件（単一標準Edit Layerなど）を満たさないLandscapeでは使えません。PIE／Save／GC中も拒否します。

GetHeightは元Heightfieldのbilinear値であり、Collision高さそのものではありません。編集成功は歩行可能性を保証しません。急斜面へのCharacter前進阻止は正常なCharacterMovement動作となる場合があります。

## v0.4の3 Toolの制限

- **Dry-runの成功はApplyの受理を保証しません。** Applyだけが、Editorのtransaction、Autosave・自動checkout・Source Control、materialとGrass、collisionの状態、書き込み直前の地形の一致を検査します。
- Applyできるのは、collisionがHeightfieldと同じ解像度（Collision Mip Level・Simple Collision Mip Levelが0、material position offsetのbakeなし）で、materialがEngineのdefault materialのLandscapeだけです。独自のmaterial、Material Instance、Landscape Grassは非対応です。
- `FlattenRectangle`の`yawDegrees`は0だけ受理します。360なども正規化しません。
- 経路は2〜64点、長さ10000cmまで。1cm未満のsegment、自己交差、帯どうしの重なり、U-turn、直角以上の折れを拒否します。幅に対して格子の支持が足りない要求も拒否し、自動で幅を広げません。
- 幅・矩形の辺は5000cm、falloffは2500cm、`maxDeltaCm`は1000cm、`maxSlopeDegrees`は60度まで。要求頂点・読み取り頂点は16384、profileは1024点まで。
- `CreateWalkablePath`は経路探索をしません。端点の高さは変えません。条件を満たす計画が作れても、量子化後の最終確認で拒否することがあります（安全側に倒しています）。
- `CreateWalkablePath`が保証するのは、道の中心部（core）の三角形の傾斜が、元データと実際のcollisionの両方で閾値を下回ることです。CharacterMovement、NavMesh、Capsuleの大きさ、Step Heightは扱いません。
- 同じrequestの再送が`NO_CHANGE`になるのは、直近128件の記録に残っていて、地形が記録した結果と一致する場合だけです。記録が消えた後（128件より古い、またはEditor再起動後）の同じrequestは新しい編集として実行され、falloffの部分はもう一度blendされます。
- collisionの検証で読み飛ばせる他のcomponentは256個までです。propは動かさず、地形の変更に追従しません。
- 周辺への副作用の確認は、Actor一覧、各Actor自身のproperty、対象Landscapeのweightmapなどの比較です。他Actorのcomponent内部の状態まで毎回比較しているわけではありません。
- rollback後に元へ戻ったことを証明できなかった場合、3 ToolのApplyはEditorを再起動するまで使えません。MCPから解除する方法はありません。
- collisionの量子化の計算はUE5.8のChaos heightfieldの実装に合わせています。別のEngineバージョンではApplyが拒否される可能性があります。

## v0.5のFoliage Safetyの制限

- Foliageを動かす・消す・置き直す編集はありません。影響がある編集は拒否するだけです。
- 判定できないものとして拒否する形式・状態: Landscape Grass、Procedural Foliage、Actor Foliage、対象と別のLevel（非表示・ロック中を含む）のFoliage、読み込まれていないstreaming level、走査上限（読み取り範囲に重なる65,536 instance）の超過、付属先を証明できないinstance。
- `FoliageLandscapeSnappingMode`が`DontSnap`以外で、対象Landscapeに付属したFoliageがある場合はApplyを拒否します。設定は変更しません。
- 円形ブラシの3 Toolは、付属していない（`BaseId`が無効な）Foliageを保護しません。書き込み後のFoliageの検証もありません。
- 付属の判定は、評価のたびに全LevelのFoliageの全instanceを走査します（上限なし）。数十万instance以上のWorldでは、1回の評価に数msから数十msかかる場合があります。
- 閾値は固定です（高さは量子化の半段、傾斜は0.01度）。入力では変えられません。
- Editor worldでは、読み込まれていないstreaming levelへの参照があっても、Editor自身が次のframeで読み込みます。拒否の判定はToolを呼んだ時点の状態で行います。

## Landscape Grass

v0.5.0では、Landscape Grass Output（またはGrass Type）を使うLandscapeへの編集は非対応です。

- v0.4の3 ToolのApplyは`UNSUPPORTED_GRASS_STATE`で拒否します。Dry-runの`foliage`では`LANDSCAPE_GRASS`として`UNKNOWN`になります。
- 円形ブラシの3 ToolのApplyも、Grassの状態がある、またはmaterialの中を確かめられないLandscapeを拒否します（`message`の先頭が`UNSUPPORTED_GRASS_STATE:`。v0.5.0 RC2で追加。v0.3〜v0.5.0 RC1は検査していませんでした）。Dry-runは検査しません。
- Grassの状態: material（Material Instanceの親、Material Function・Function Instance・material layerの関数を、入れ子や共有も含めて最後まで辿る）のLandscape Grass Output、ActorのGrass Type override、Componentのgrass type、生成済みのGrassのcomponentと非同期task。
- 確かめられない構成として拒否するもの（v0.5.0 RC3で拡大）: 親のないMaterial Instance・Material Function Instance、関数を指定していないFunction Call、参照の循環（Material Instanceの親、関数の呼び出し）、editor dataのないmaterial・material function、解析の上限（参照の深さ64、material・function 4096、expression 262,144）の超過、material layerを使うmaterialのMaterial Instance（layerの差し替え先を確かめないため）。空のlayer slotや、LODの上書きの空slotは拒否しません。解析は読み取りだけで、materialを変更・修復しません。
- 判定の後も、書き込む直前と書き込んだ直後に、Grassの状態とmaterialが判定時と同じことを確かめます。Editor拡張などが`Modify`の通知の中でmaterialやGrassの状態を変えた場合、書き込む前なら何も書き込まずに拒否し、書き込んだ後なら本Pluginのtransactionを1件だけUndoして拒否します（`message`はどちらも`UNSUPPORTED_GRASS_STATE:`で始まります）。Undoの後に高さが元に戻り、Grassのinstanceが生成されていないことを確かめられない場合は、成功を返さず、そのmodule sessionの間は円形ブラシの書き込みを止めます（`BRUSH_WRITES_DISABLED:`）。
- Grassのない独自materialは、円形ブラシで編集できます。GrassはUEが地形の変更後に再生成するため、Grassのinstanceは保護も検証もされません。
- 将来の検討事項（未実装・時期未定）: Grassを使うLandscapeへの安全な編集を、将来のバージョンで検討します。特定のバージョンでの実装は約束していません。

## Water System・Spline・保護領域

v0.5.0は、Unreal EngineのWater SystemやSplineを使った川・道路などの自動検出、保護領域（編集禁止領域）の設定、地形の変更に合わせた自動追従をサポートしていません。これらを検出・保護・追従する機能はありません。

これらを含む地形について、LandscapeMCPは関連するActorや形状が適切に維持されることを保証しません。川岸、道路、橋、建物などに影響する地形の編集は避けてください。

将来の検討事項（未実装・時期未定）: 編集禁止領域の設定、Water System・Splineへの影響の評価、安全な自動追従を、段階的に検討します。実装するバージョンや時期は未定で、特定のバージョンでの実装は約束していません。

## 外部制約

公式StartPIEが`PIE ended before warmup completed.`と返した直後にPIEが起動済みのケースを確認しました。LandscapeMCPの不具合として分類していません。状態を確認し、無条件retryを避けます。

UE5.8／Windows 11／Win64 Editor以外は未検証です。検証に使用したVisual Studio 14.51 toolchainは、Engine推奨の14.50より新しいというBuild警告が出ていました。
