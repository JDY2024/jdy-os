# Step 2-3: ELFセグメントの固定配置

`bootloader/main.c`の`load_segments()`は、ELFの全検証と配置計画を完了してから、
`AllocatePages(AllocateAddress, EfiLoaderCode, ...)`で全範囲を確保します。
全確保の成功後、各PT_LOADのファイル部分をコピーし、残りをゼロ初期化します。
一時ファイルバッファは解放しますが、成功したロード先ページは保持します。
確保途中の失敗では、確保済み範囲を逆順に解放し、ロード先へコピーしません。

ページ範囲は開始アドレス順に並べ、重複・隣接を統合します。
同一ページを共有するセグメントも二重確保しません。
バイト範囲自体の重複は、コピーとゼロ初期化の競合を避けるため拒否します。

対応形式はELF64 / Little Endian / x86-64 / ET_EXECです。
`p_paddr == p_vaddr`を要求し、下位canonicalアドレスの範囲だけを扱います。
NULLページ、範囲計算やページ切り上げで上限を超える領域は拒否します。
PT_DYNAMIC、PT_INTERP、Program Header個数の拡張形式は未対応です。
エントリーは空でないPF_Xセグメント内にある必要があります。
ELF属性からのページ保護設定、Kernelへのジャンプ、ExitBootServices、再配置は行いません。

## ビルドとホストテスト

```sh
make
readelf -h -l build/kernel.elf
clang -fshort-wchar -std=c11 -Wall -Wextra -Werror \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I/usr/include/efi -I/usr/include/efi/x86_64 \
  tests/elf_loader_test.c -o /tmp/jdy-elf-test
/tmp/jdy-elf-test
```

ホストテストは実際の`load_segments()`を呼び出し、UEFIサービスをモックします。
コピー、ゼロ初期化、共有ページ、逆順のセグメント、隣接ページ、
確保失敗時のロールバック、不正なELFを含む28ケースを確認します。
テスト用にホストの`0x100000`へメモリを確保します。既存マッピングを上書きしない
`MAP_FIXED_NOREPLACE`を使うため、使用中ならテストは失敗します。

## QEMUでの確認

```sh
make run
```

期待する表示:

```text
AllocatePages requested=0x0000000000100000 pages=0x0000000000000001
Allocated address=0x0000000000100000
PT_LOAD index=0x0000000000000000 destination=0x0000000000100000 filesz=0x000000000000000A memsz=0x000000000000000A
Kernel segments loaded successfully
```

表示後は既存の5秒間の待機を経てUEFIへ戻ります。
`0x100000`を確保できない環境では、要求アドレスと失敗ステータスを表示して終了します。

今回のQEMU + OVMF検証では、元のESPとOVMF変数ファイルへの書き込みを避けるため、
一時FATイメージと変数ファイルのコピーを使用しました。
QMPの`xp`と`pmemsave`で、次の一致を確認しました。

| 項目 | 結果 |
|---|---|
| ELFのコピー元 | ファイルオフセット`0x1000`から10バイト |
| Program Headerの配置先 | `p_paddr = p_vaddr = 0x100000` |
| 実際の確保 | `0x100000`、1ページ |
| 実メモリの10バイト | `55 48 89 e5 eb 00 fa f4 eb fc`、ELFと完全一致 |
| エントリー | `0x100000`、PF_Xセグメント内 |

追加の一時ELFでは、`0x100000`と`0x100100`に2セグメントを置き、
1ページだけを確保することを確認しました。それぞれ10/16バイト、4/8バイトの
FileSiz/MemSizで、コピー内容とゼロ初期化された末尾を実メモリで確認しました。
この検証用ELFはプロジェクトの`kernel.elf`を変更していません。

## GDBによるメモリ照合

`make run`に相当するQEMUコマンドに`-s -S`を追加すると、起動時に停止し、
localhost:1234でGDBを待ち受けます。別ターミナルで接続します。

```text
gdb build/kernel.elf
(gdb) target remote localhost:1234
```

今回の10バイトのKernelでは、最後の2バイトの書き込みを監視すると、
ファームウェアの早期処理と区別して、コピー完了の時点で停止できます。
値はKernelの再ビルドで変わるため、その場合はELFの実バイトに合わせてください。

```text
(gdb) watch *(unsigned short *)0x100008
(gdb) condition 1 *(unsigned short *)0x100008 == 0xfceb && *(unsigned long long *)0x100000 == 0xf4fa00ebe5894855
(gdb) continue
(gdb) x/10bx 0x100000
(gdb) x/5i 0x100000
(gdb) dump binary memory /tmp/jdy-loaded.bin 0x100000 0x10000a
```

Kernelは実行しないため、`break KernelMain`には到達しません。
今回もこのウォッチポイントで停止し、GDBから読み出した10バイトの一致と、
`push; mov; jmp; cli; hlt`の逆アセンブルを確認しました。
実行環境でソケットが制限されていたため、検証時のGDB接続は
`target remote | qemu-system-x86_64 ... -gdb stdio -S`のパイプ経由で行いました。

ダンプとELFの比較:

```sh
python3 - <<'PY'
from pathlib import Path
elf = Path('build/kernel.elf').read_bytes()
assert Path('/tmp/jdy-loaded.bin').read_bytes() == elf[0x1000:0x100a]
print('ELFとロード先メモリが一致')
PY
```
