# ZMK BLE Link Policy

[English](README.md)

選択中の BLE HID ホストを優先し、ほかの既知ホストとの接続には peripheral
latency 30 を要求するモジュールです。stock ZMK と DYA の公開 endpoint・profile API、
および Zephyr の接続 API を使い、コアの修正や関数の置き換えを行いません。

## 導入

既存の ZMK import を保ったまま、`config/west.yml` にプロジェクトを追加します。

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-ble-link-policy
      remote: amgskobo
      revision: main
```

`west update zmk-ble-link-policy` を実行し、キーボードを再ビルドします。
再現可能な本番ビルドでは `main` をレビュー済みの完全なコミット ID に置き換えてください。

ローカル導入では共通ビルドスクリプトに `-m /absolute/path/zmk-ble-link-policy` を追加するか、
ビルドで明示する完全な `ZMK_EXTRA_MODULES`／`ZEPHYR_MODULES` 一覧へルートを含めます。
モジュールを明示選択するビルドでは、west プロジェクトの追加だけでは選択に入りません。

ホストと接続する standalone または split central で有効にします。

```conf
CONFIG_ZMK_BLE_LINK_POLICY=y
CONFIG_BT_PERIPHERAL_PREF_MIN_INT=12
CONFIG_BT_PERIPHERAL_PREF_MAX_INT=12
CONFIG_BT_PERIPHERAL_PREF_LATENCY=0
CONFIG_BT_PERIPHERAL_PREF_TIMEOUT=400
```

間隔の単位は 1.25 ms（12 は 15 ms、24 は 30 ms）、latency は省略可能な接続イベント数、
timeout の単位は 10 ms（400 は 4 秒）です。この例は既存の接続設定を維持します。
モジュールは既定で無効です。`ZMK_BLE` と `BT_PERIPHERAL` が必要で、split peripheral
は対象外です。依存機能を自動で有効にしません。
`CONFIG_ZMK_BLE_LINK_POLICY_LOG_LEVEL` は 0～4、既定値は 3 です。

管理する論理プロファイルは 0～4 の5個です。プロファイルを増やしても表は拡張されません。
プロファイルや接続オブジェクトを提供するモジュールではないため、ホスト・split・広告・
再接続に必要な `BT_MAX_CONN` の容量は統合するキーボード側で確保します。

### 互換性とテスト

以下のコミットを変更せずにビルド検証します。

| コア | 検証対象のリビジョン |
| --- | --- |
| Stock ZMK | `5b51501fead672c41b5cfb396f3dafe0894bf4e9` |
| DYA | `e5c9b6915b56801193e359dd9bad4a167ce0d1b8` |
| 共通 Zephyr | `10ba6d0cb38bc3d258775d27982f707599320085` |

[テスト用の固定一覧](tests/ci/pins.json)では補助ライブラリ・コアのビルド依存・
SDK イメージも固定します。他のリビジョンには個別の検証が必要です。
隣接リポジトリ、AKKB46 の設定、非公開キャッシュへの依存はありません。
[CI の再現手順](tests/README.md#reproducing-ci)、[テスト契約](tests/maintenance.json)、
[検証と実機受け入れの手順](docs/validation.md)を参照してください。
ホスト上の CI を実行する前に成功バッジを掲示することはしません。

## 接続ポリシー

実際に選択されている HID 出力先が BLE の場合だけ、そのホストを active とします。
USB または出力先なしの場合、active な BLE HID ホストはありません。
既知のホストプロファイルに属し、default identity の peripheral として接続中のリンクだけを
扱います。split 用の central 接続、ゲスト、別 identity、切断済みの接続は除外します。
アプリケーションの通信では昇格させません。

選択先を先に処理し、上記 `BT_PERIPHERAL_PREF_*` の値を要求します。ほかのホストには
latency 30 を要求し、設定済みの supervision timeout を維持します。idle の間隔下限は
`max(MIN_INT,12)` です。初回の上限は `max(MAX_INT,下限)`、適用を確認できなかった接続の
次回以降は `max(MAX_INT,24)` とし、切断まで維持します。許容範囲内の実測パラメーターで
適用を判断します。選択先の設定下限が 12 未満で、2 回の要求と反映待ちを経ても適用されない
場合は、切断まで 12～`max(MAX_INT,12)` へ移行します。下限 12 以上ではこの移行を行いません。

昇格に idle 化の猶予は加えません。旧選択先の idle 化は最後の出力先変更から 6 秒待ちます。
各モードの要求は最大 3 回で、要求間隔には独立した 5 秒の制限、API が受け付けた要求には
20 秒の反映待ち上限があります。API の成功と、相手が値を適用したことは別です。
適用通知、切断世代、古い走査の無効化を用いて最新の選択を維持します。
相手は要求を拒否・変更でき、スタックが受け付けた要求をこのモジュールから取り消すことはできません。

広告・プロファイル・ボンド・PHY・パケット長・転送セッションは変更しません。
プラットフォームの接続直後の自動更新は引き続き存在します。
別のアプリ側ポリシーが同じリンクへ矛盾した要求を出さないよう、統合時に調整してください。

## 資源と確認範囲

system workqueue の1回の処理で発行する接続パラメーター要求は最大1件です。
上限による保留では試行回数を消費せず、すぐに続行を予約します。続行時に現在の
出力先を読み直し、選択ホストを優先します。最大3回、要求間隔5秒、受理後の待機
20秒、idle 化の猶予6秒は維持します。制限するのは呼び出し回数であり、個々の
同期 API の処理時間ではありません。スレッドや idle 時の定期処理は追加しません。

測定時は `CONFIG_ZMK_BLE_LINK_POLICY=y` と `CONFIG_LOG=y` に加えて
`CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS=y` を設定し、ログレベルを
`CONFIG_ZMK_BLE_LINK_POLICY_LOG_LEVEL=3` にします。診断は既定で無効です。
4バイトの連番、一時的な計数・時刻取得、要求を出した完了処理ごとの数値ログ
`linkwork v=1` が増えます。Flash・RAM とログの負荷は実際のリンク結果で確認します。
時間は割り込みや待機を含むマイクロ秒で、末尾の集計ログ出力は含みません。
CPU 使用時間や無線占有率ではありません。`invalid` は時刻差の曖昧さや桁あふれ、
`pending_ms=4294967295` は予約なし、`active_slot=255` は選択 BLE ホストなしを表します。
`host_mask` は走査できた対象ホストで、古くなった走査では部分的な値です。
選択中のプロファイルが 0～4 の範囲外の場合も `active_slot=255` として記録します。
要求なしの処理でも連番は進むため、欠番だけでログ欠落とは判断できません。
[観測手順](tests/README.md)に従い、同じファーム・ホスト・選択先・操作で比較し、
実際のホスト HID 到着も測ってから応答改善を判断してください。

固定の 5 スロット状態表、spinlock 1 個、遅延 work 1 個を使います。走査をまたぐ接続参照、
ヒープ、新しいスレッドはなく、調整が終わればタイマーを止めます。再試行は既存の system
workqueue で行います。ログはスロットと接続条件の数値だけで、アドレスや本文を含みません。
Flash／RAM の増減は実際のリンク結果とログ設定で確認してください。
同じ処理の移設だけで速度向上や省電力を実測したことにはなりません。

実際の config・コンパイル対象・シンボルを確認し、統合先の左右と settings-reset をビルドします。
同一性を確認したファームウェアで、ホスト HID 到着、プロファイル・USB 切替、複数ホスト、
拒否・再接続、split、消費電力を再測定する必要があります。
ホスト・カーネル・ビルド検証では、これらの実機受け入れは成立しません。

[テスト手順](tests/README.md)を参照してください。

## 問題の切り分け

- 設定シンボルが未定義なら、モジュールの選択と実際の `.config`・コンパイル対象を確認します。
- split peripheral は意図的に対象外です。検証したコアでは依存関係の警告を出し、
  ポリシーのソース・シンボルを含めずにビルドします。
- USB 選択中は active な BLE ホストがありません。キーボード側の操作で BLE 出力先を選びます。
- API の受理と実際の条件変更は別です。適用通知またはホストの接続条件を確認し、
  要求間隔・相手の制約・workqueue の実行時間を分けて判断します。
- `linkwork` がない場合、完了した要求処理を観測できていません。診断設定・INFO ログ・
  コンソール・起動識別子を確認し、未観測を処理時間ゼロとして扱わないでください。

## ライセンス

GPL-3.0-only です。[LICENSE](LICENSE)を参照してください。
