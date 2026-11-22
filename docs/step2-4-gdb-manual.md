# Step 2-4 — UEFI から KernelMain へ制御を移す

Step 2-3 では、ブートローダーが ELF の `PT_LOAD` セグメントを RAM に配置しました。Step 2-4 では、UEFI の Boot Services を終了し、CPU が RAM 上のカーネル命令を実行するところまで確認します。

## ELF のエントリポイント

`e_entry` は ELF ヘッダーに記録された実行開始時の仮想アドレスです。リンカスクリプトの `ENTRY(KernelMain)` が開始シンボルを指定し、現在のビルドではリンカが `0x100000` を記録します。次のコマンドで ELF ヘッダーとセグメントを確認できます。

```sh
make
readelf -hW build/kernel.elf
readelf -lW build/kernel.elf
```

ブートローダーは、`e_entry` が実行フラグ `PF_X` を持つ `PT_LOAD` のメモリ範囲内にあることを確認し、全セグメントを配置できた時点でその値を返します。このローダーでは仮想アドレスと物理アドレスが一致するセグメントを扱うため、現在のページテーブルでコピー先をそのまま命令の実行先として使います。

## メモリマップと Boot Services の終了

`GetMemoryMap()` は、利用可能な RAM やファームウェア用の予約領域などを記述子の配列として返します。併せて返る `MapKey` はその時点のマップを識別する値です。ページやプールを確保・解放するとマップが変化し、以前のキーが無効になることがあります。

1. ELF の読み込み、ファイルのクローズ、一時バッファの解放、コンソール出力を済ませます。
2. `GetMemoryMap()` が返す必要サイズを基に、余裕を持たせたバッファを確保します。バッファの確保でもマップが変化するので、確保後にマップを取得し直します。
3. マップ取得に成功したら、ログやメモリ確保を挟まず、そのキーを `ExitBootServices()` に渡します。
4. `EFI_INVALID_PARAMETER` ならマップとキーを取り直して再試行します。再取得で `EFI_BUFFER_TOO_SMALL` が返った場合は、バッファを拡張してから改めて取得します。最初の終了呼び出しでファームウェアが一部のサービスを停止することがあるため、再試行ではメモリ関連サービスだけを使います。最初の終了試行以降に回復できないエラーが発生した場合は CPU を停止します。

`ExitBootServices()` が成功すると、起動後のシステム管理は OS 側に移ります。以降は `AllocatePages()`、`FreePool()`、`GetMemoryMap()`、`Stall()` などの Boot Services や、UEFI のファイル操作、`ConOut->OutputString()` などの起動時プロトコルを使えません。UEFI Runtime Services は別のインターフェースです。 [UEFI 仕様 §7](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html), [EFI System Table](https://uefi.org/specs/UEFI/2.10/04_EFI_System_Table.html)

## CPU が KernelMain に入るしくみ

UEFI の呼び出しは Microsoft x64 ABI、ELF の `KernelMain` は System V AMD64 ABI に従います。ブートローダーは `sysv_abi` を指定した関数ポインタを使い、Clang にカーネル側の規約で呼び出しを生成させます。引数はありませんが、スタックのアラインメントなどの規約は適用されます。 [System V AMD64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex)

Boot Services の終了に成功した後、`cli` でマスク可能な割り込みを無効にし、`cld` で方向フラグをクリアします。間接 `call` は既存のスタックに戻りアドレスを置き、CPU の命令ポインタ `RIP` を `e_entry` に移します。カーネルに引数は渡さず、既存のスタックとページテーブルを引き継ぎます。`KernelMain` は `cli` と `hlt` を含むループに入り、`hlt` で CPU の命令実行が停止します。再開する事象があっても、ループ先頭へ戻ります。

## GDB での確認

ターミナル 1 で、QEMU をリセット直後の停止状態で起動します。画面を使えない環境やリセットを記録したい場合は、2 行目を代わりに使います。

```sh
make debug
# または
make debug QEMU='qemu-system-x86_64 -display none -no-reboot -d cpu_reset,int -D build/step2-4-qemu.log'
```

ターミナル 2 で GDB を起動します。

```sh
gdb -q build/kernel.elf
```

```gdb
target remote 127.0.0.1:1234
hbreak *0x100000
continue
info registers rip rsp eflags
p/x ((unsigned long long)$rsp & 0xf)
x/6i 0x100000
```

`hbreak` は命令の内容を書き換えずに実行アドレスで停止できるため、カーネルのロード前に設定できます。`continue` 後は `KernelMain` の先頭で止まり、`RIP = 0x100000`、関数入口での `RSP & 0xf = 0x8` が期待値です。コード上、このエントリに制御が移るのは `ExitBootServices()` が成功した場合です。現在のビルドでは次の命令列が見えます。

```text
0x100000    push   %rbp
0x100001    mov    %rsp,%rbp
0x100004    jmp    0x100006
0x100006    cli
0x100007    hlt
0x100008    jmp    0x100006
```

`stepi` を繰り返し、`x/i $pc` で次の命令が `hlt` になるまで進めます。このビルドでは 4 回です。そこで `info registers eflags` を実行すると `IF` がクリアされていることを確認できます。`continue` で数秒動かしてから GDB で Ctrl+C を押し、続けて次を実行します。

```gdb
info registers rip eflags
monitor info registers
```

停止状態なら QEMU モニターに `HLT=1` が表示され、`RIP` は通常 `hlt` の次の命令 `0x100008` を指します。ログを有効にして起動した場合は、起動時の初期リセットに続く追加の `CPU Reset` や `Triple fault` がないことも確認します。

## よくあるエラー

| 症状 | 確認すること |
| --- | --- |
| GDB が接続できない | `make debug` が動作中か、ポート 1234 が既に使用中でないか確認します。 |
| 入口のブレークポイントに到達しない | `readelf -hW` で現在のエントリを確認し、`make` で ESP 上のカーネルを更新します。QEMU の画面で ELF 検証やページ割り当てのエラーも確認します。 |
| `EFI_INVALID_PARAMETER` が返る | マップ取得と終了呼び出しの間にログ、割り当て、解放が入っていないか確認します。実装は最新のキーで再試行します。 |
| `EFI_BUFFER_TOO_SMALL` が返る | サイズを問い合わせる初回は正常です。確保後も返る場合は、返されたサイズでバッファを拡張し、マップを取り直します。 |
| 画面が最後のログのまま | この段階のカーネルは画面に出力せず停止します。GDB の `RIP` と QEMU の `HLT` を確認します。 |
| QEMU が終了・再起動する | `-no-reboot` とログを使って例外やリセットを調べます。エントリのアドレス、スタックのアラインメント、命令列を確認します。 |
