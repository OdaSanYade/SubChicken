# input_actions.json 記述ガイド

`KdInputActionSystem::LoadActionDefinitions()` / `LoadBindings()` が読み込むJSONの、
**書き方だけ**にしぼった詳しいリファレンスです。

---

## 目次

1. [ファイルの基本構造](#ファイルの基本構造)
2. [1アクションの構造](#1アクションの構造)
3. [共通フィールド](#共通フィールド)
4. [type別リファレンス](#type別リファレンス)
   - [button](#button-キーパッドボタンの押下)
   - [triggerButton](#triggerbutton-ltrtをボタン扱い)
   - [axis](#axis-スティック上下左右キーの2次元入力)
   - [mouseAxis](#mouseaxis-マウス移動量を軸に)
   - [triggerAxis](#triggeraxis-ltrtのアナログ値)
5. [名前テーブル（キー名・ボタン名）](#名前テーブルキー名ボタン名)
6. [書き方のパターン集](#書き方のパターン集)
7. [よくある間違いと検証の挙動](#よくある間違いと検証の挙動)
8. [完成版サンプル（コピペ用）](#完成版サンプルコピペ用)

---

## ファイルの基本構造

トップレベルは必ず **`"actions"` という配列を1つ持つオブジェクト** です。

```json
{
  "actions": [
    { /* アクション1 */ },
    { /* アクション2 */ }
  ]
}
```

- `"actions"` が無い、または配列でない場合は読み込み失敗（`false` を返し、Debugではassert）。
- 配列の順番に意味はありません（`name` で参照するため）。
- JSONなのでコメントは書けません（`//` や `/* */` は不可）。末尾カンマも不可。

> `LoadActionDefinitions`（アクション定義の読込）と `LoadBindings`（保存済みキーコンフィグの復元）は **同じスキーマ** を使います。違いは、前者が新規登録、後者が「登録済みアクションへの上書き」である点だけです。

---

## 1アクションの構造

各アクションは、共通フィールド＋`type`に応じた専用フィールドで構成します。

```json
{
  "name": "Jump",        // 必須：この名前でコードから参照する
  "type": "button",      // どの種類の入力か
  "player": 0,           // ゲームパッド番号
  "keyboard": ["Space"], // type固有のフィールド
  "gamepad": ["A"]       // type固有のフィールド
}
```

コードからは `name` で参照します。

```cpp
input.IsPress("Jump");     // buttonやtriggerButton
input.GetAxis("Move");     // axis/mouseAxis/triggerAxis
```

---

## 共通フィールド

すべての`type`で使えるフィールドです。

| フィールド | 型 | 必須 | 既定値 | 説明 |
| --- | --- | --- | --- | --- |
| `name` | 文字列 | ✅ | — | アクション名。空文字だとその要素は無視される |
| `type` | 文字列 | — | `"button"` | 入力の種類（下記5種）。省略すると button 扱い |
| `player` | 整数 | — | `0` | ゲームパッド番号（0〜3）。キーボード/マウスのみのアクションでは無視される |

`type` に指定できる値:

| type | 分類 | コードでの取得 |
| --- | --- | --- |
| `button` | ボタン | `IsPress` / `IsHold` / `IsRelease` / `IsFree` |
| `triggerButton` | ボタン | 同上 |
| `axis` | 軸 | `GetAxis`（x,y） |
| `mouseAxis` | 軸 | `GetAxis`（x,y） |
| `triggerAxis` | 軸 | `GetAxis`（xのみ） |

---

## type別リファレンス

### `button` （キー／パッドボタンの押下）

キーボードキーとゲームパッドボタンを紐づけます。両方書けば **どちらでも反応**（OR）します。

| フィールド | 型 | 既定 | 説明 |
| --- | --- | --- | --- |
| `keyboard` | 文字列の**配列** | なし | キー名の配列。配列内の複数キーもORになる |
| `gamepad` | 文字列の**配列** | なし | ボタン名の配列。複数指定は同時押しではなくOR |

```json
{
  "name": "Jump",
  "type": "button",
  "keyboard": ["Space", "Enter"],
  "gamepad": ["A"],
  "player": 0
}
```

- `keyboard` を省略 → キーボード未割り当て（パッド専用）
- `gamepad` を省略 → パッド未割り当て（キーボード専用）
- **どちらも配列** である点に注意（`"keyboard": "Space"` は不可、`["Space"]` と書く）

キーボード専用の例:

```json
{ "name": "Pause", "type": "button", "keyboard": ["Escape"] }
```

パッド専用の例:

```json
{ "name": "Menu", "type": "button", "gamepad": ["Start"], "player": 0 }
```

---

### `triggerButton` （LT/RTをボタン扱い）

アナログトリガーを、しきい値を超えたら「押した」とみなすボタンにします。

| フィールド | 型 | 既定 | 説明 |
| --- | --- | --- | --- |
| `gamepadTrigger` | 文字列 | `"RightTrigger"` | `"LeftTrigger"` / `"RightTrigger"` |
| `threshold` | 数値 | `0.5` | 0.0〜1.0。この踏み込み量を超えたらPress判定 |

```json
{
  "name": "Dash",
  "type": "triggerButton",
  "gamepadTrigger": "RightTrigger",
  "threshold": 0.6,
  "player": 0
}
```

- `keyboard` は使えません（トリガー専用）。キーボードにも割り当てたい場合は、同名の `button` アクションを別に追加する（[パターン集](#書き方のパターン集)参照）。

---

### `axis` （スティック／上下左右キーの2次元入力）

移動やカメラなど、2次元ベクトルで欲しい入力に使います。

| フィールド | 型 | 既定 | 説明 |
| --- | --- | --- | --- |
| `keyboard` | **オブジェクト** | なし | `{ "up":..., "right":..., "down":..., "left":... }`。各方向は文字列1つ |
| `gamepadStick` | 文字列 | `"LeftStick"` | `"LeftStick"` / `"RightStick"` |
| `deadzone` | オブジェクト | 機種標準 | `{ "inner":0.2, "outer":0.98 }` |
| `invertY` | 真偽値 | `false` | Y軸反転（`true`で上下逆） |

```json
{
  "name": "Move",
  "type": "axis",
  "keyboard": { "up": "W", "right": "D", "down": "S", "left": "A" },
  "gamepadStick": "LeftStick",
  "player": 0,
  "deadzone": { "inner": 0.2, "outer": 0.98 },
  "invertY": false
}
```

**`keyboard` はオブジェクト**（`button`の配列とは違う点に注意）。使わない方向は省略できます:

```json
"keyboard": { "left": "A", "right": "D" }
```

`deadzone`:

| キー | 意味 |
| --- | --- |
| `inner` | これより内側の傾きは無視（スティックのドリフト除去）。省略／負値で機種標準デッドゾーンを使用 |
| `outer` | これより外側は最大(1.0)扱い。省略時 1.0 |

内側〜外側の間は 0.0〜1.0 へ滑らかに正規化されるため、境界での値の飛びが起きません。
迷ったら `deadzone` ごと省略すれば機種標準値が使われます。

出力（`GetAxis`）:

- `x` = 左右（右が +）
- `y` = 上下（**上が +**。画面座標とは逆）

---

### `mouseAxis` （マウス移動量を軸に）

マウスの移動量を軸として取得します。カメラ操作などに。

| フィールド | 型 | 既定 | 説明 |
| --- | --- | --- | --- |
| `fixKey` | 文字列 | なし | このキーを押している間だけ、押した位置を中心にした疑似スティックになる |

```json
{ "name": "Look", "type": "mouseAxis", "fixKey": "RightMouse" }
```

- `fixKey` を省略すると、常時マウスの1フレーム移動量を拾います。
- `player` は無関係（キーボード/マウス系のため）。

---

### `triggerAxis` （LT/RTのアナログ値）

トリガーの踏み込み具合を 0.0〜1.0 のアナログ値で取得します。

| フィールド | 型 | 既定 | 説明 |
| --- | --- | --- | --- |
| `gamepadTrigger` | 文字列 | `"RightTrigger"` | `"LeftTrigger"` / `"RightTrigger"` |

```json
{ "name": "Accel", "type": "triggerAxis", "gamepadTrigger": "RightTrigger", "player": 0 }
```

出力は `GetAxis("Accel").x` に 0.0〜1.0（`y` は常に0）。

---

## 名前テーブル（キー名・ボタン名）

文字列で指定できる名前の一覧です（`KdInputKeyNames` 準拠）。**大文字小文字・スペルは完全一致**が必要です。

### キーボード（`keyboard` / `fixKey`）

| 分類 | 使える名前 |
| --- | --- |
| 英字 | `A` 〜 `Z` |
| 数字 | `0` 〜 `9` |
| ファンクション | `F1` 〜 `F12` |
| 特殊 | `Space` `Enter` `Escape` `Tab` `Backspace` |
| 修飾（左右共通） | `Shift` `Ctrl` `Alt` |
| 修飾（左右個別） | `LeftShift` `RightShift` `LeftCtrl` `RightCtrl` `LeftAlt` `RightAlt` |
| 矢印 | `Up` `Down` `Left` `Right` |
| マウス | `LeftMouse` `RightMouse` `MiddleMouse` |

### ゲームパッドボタン（`gamepad`）

| 分類 | 使える名前 |
| --- | --- |
| フェイス | `A` `B` `X` `Y` |
| ショルダー | `LeftShoulder` `RightShoulder` |
| スティック押し込み | `LeftThumb` `RightThumb` |
| システム | `Start` `Back` |
| 十字キー | `DPadUp` `DPadDown` `DPadLeft` `DPadRight` |

### スティック（`gamepadStick`）

`LeftStick` / `RightStick`

### トリガー（`gamepadTrigger`）

`LeftTrigger` / `RightTrigger`

> ここに無いキー（テンキーや記号キーなど）を使いたい場合は、`KdInputKeyNames.cpp` の対応表に1行追加すれば使えるようになります。

---

## 書き方のパターン集

### 1つの操作をキーボードとパッド両方で受ける

`button` に両方書くだけ:

```json
{ "name": "Attack", "type": "button", "keyboard": ["Z"], "gamepad": ["X"], "player": 0 }
```

### 1つのアクション名に複数の入力方式を同時に持たせる（同名OR）

**同じ `name` のエントリを複数書く** と、デバイスをまたいでOR合成されます。
例：`Look` をマウスでも右スティックでも動かせるようにする:

```json
{ "name": "Look", "type": "mouseAxis", "fixKey": "RightMouse" },
{ "name": "Look", "type": "axis", "gamepadStick": "RightStick", "player": 0, "invertY": false }
```

スティック側の `Look` は `keyboard` を書かないので、WASDでカメラが動く事故にはなりません。

### トリガーをキーボードにも割り当てる

`triggerButton` は `keyboard` を持てないので、同名の `button` を足す:

```json
{ "name": "Dash", "type": "triggerButton", "gamepadTrigger": "RightTrigger", "threshold": 0.6, "player": 0 },
{ "name": "Dash", "type": "button", "keyboard": ["LeftShift"] }
```

### 2人プレイ（player違い）

同じ操作名でも `player` を分けて別アクションにする（名前を変えるのが安全）:

```json
{ "name": "P1_Jump", "type": "button", "gamepad": ["A"], "player": 0 },
{ "name": "P2_Jump", "type": "button", "gamepad": ["A"], "player": 1 }
```

### カメラのY軸を反転する

```json
{ "name": "Look", "type": "axis", "gamepadStick": "RightStick", "player": 0, "invertY": true }
```

---

## よくある間違いと検証の挙動

| 症状 | 原因 | 対処 |
| --- | --- | --- |
| 読み込み自体が失敗（`false`） | JSON構文エラー（末尾カンマ、コメント、括弧閉じ忘れ等）／`"actions"` が無い | 構文を見直す。Debugでは解析失敗のassertが出る |
| 特定のキーだけ効かない | キー名のスペル/大文字小文字ミス（`space`→正しくは`Space`） | [名前テーブル](#名前テーブルキー名ボタン名)と完全一致させる |
| assert「未知のキー名です」 | 対応表に無い名前を指定 | 名前を修正、または `KdInputKeyNames.cpp` に追加 |
| assert「未知のtypeです」 | `type` の綴り違い（`Button`→`button`） | typeは小文字の5種のみ |
| `axis`の`keyboard`が効かない | 配列で書いている | `axis`の`keyboard`は**オブジェクト** `{ "up":..., }` |
| `button`の`keyboard`が効かない | オブジェクトで書いている | `button`の`keyboard`は**配列** `["Space"]` |
| キー割り当てが保存で消える | 同名エントリを複数登録＋`SaveBindings`併用 | 保存を使う名前は同名多重登録を避ける（初期定義だけなら問題なし） |

補足:

- **キー名・ボタン名・type・stick/trigger名はすべて大文字小文字を区別** します。
- `keyboard` の型は type によって違います（`button`=配列、`axis`=オブジェクト）。ここが一番間違えやすいポイントです。
- JSONを書き換えたら、次回起動時の `LoadActionDefinitions()` が読み直すので **コードのビルドは不要** です。

---

## 完成版サンプル（コピペ用）

`Look` をマウス＋右スティック両対応にした、実用的な構成です。

```json
{
  "actions": [
    {
      "name": "Jump",
      "type": "button",
      "keyboard": ["Space", "Enter"],
      "gamepad": ["A"],
      "player": 0
    },
    {
      "name": "Attack",
      "type": "button",
      "keyboard": ["Z"],
      "gamepad": ["X"],
      "player": 0
    },
    {
      "name": "Dash",
      "type": "triggerButton",
      "gamepadTrigger": "RightTrigger",
      "threshold": 0.6,
      "player": 0
    },
    {
      "name": "Dash",
      "type": "button",
      "keyboard": ["LeftShift"]
    },
    {
      "name": "Move",
      "type": "axis",
      "keyboard": { "up": "W", "right": "D", "down": "S", "left": "A" },
      "gamepadStick": "LeftStick",
      "player": 0,
      "deadzone": { "inner": 0.2, "outer": 0.98 },
      "invertY": false
    },
    {
      "name": "Look",
      "type": "mouseAxis",
      "fixKey": "RightMouse"
    },
    {
      "name": "Look",
      "type": "axis",
      "gamepadStick": "RightStick",
      "player": 0,
      "invertY": false
    },
    {
      "name": "Accel",
      "type": "triggerAxis",
      "gamepadTrigger": "RightTrigger",
      "player": 0
    },
    {
      "name": "Pause",
      "type": "button",
      "keyboard": ["Escape"],
      "gamepad": ["Start"],
      "player": 0
    }
  ]
}
```
