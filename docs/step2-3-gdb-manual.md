# Step 2-3：GDBでELFロードを手動検証する

## 目的と検証範囲

UEFI BootloaderがELFのPT_LOADを指定アドレスへコピーしたことを、自分でGDBを操作して確認する。KernelMainへのジャンプは実装・実行しない。

コピー元はELFファイル内の `p_offset`、コピー先はゲストの `p_paddr`、コピー長は `p_filesz`。続く `p_memsz - p_filesz` バイトはゼロ化対象となる。ファイルオフセットとメモリアドレスは別の値である。

以下はプロジェクトのルートで実行する。コードブロック内の `(gdb)` は省略している。シェル用とGDB用のコマンドを混同しないこと。今回の10バイトKernelに合わせた手順であり、再ビルド後は値を確認し直す。

## 1. ターミナル1：準備とELFの確認

```sh
cd ~/workspace/jdy-os
make help
make
readelf -h build/kernel.elf
readelf -l build/kernel.elf
nm -n build/kernel.elf
objdump -d build/kernel.elf
od -An -tx1 -j4096 -N10 build/kernel.elf
cmp build/kernel.elf esp/kernel.elf
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `cd` | プロジェクトへ移動する。以降の相対パスの基準を揃える | Makefileがあるディレクトリになる |
| `make help` | 利用可能なターゲットを表示する | all / run / debug / clean / help が表示される |
| `make` | BootloaderとKernelをビルドしてESPへコピーする | エラーなく終了する |
| `readelf -h` | ELF Headerを読む。対応形式とエントリーを確かめる | ELF64、little endian、EXEC、x86-64、entry `0x100000` |
| `readelf -l` | Program Headerを読む。コピー元・先・長さを確定する | 下の表に一致する |
| `nm -n` | シンボルをアドレス順に読む。エントリーと関数の関係を確かめる | `0000000000100000 T KernelMain` |
| `objdump -d` | ファイルの命令を逆アセンブルする。GDBの比較対象を得る | 後述の6命令 |
| `od` | 4096（`0x1000`）バイト飛ばして10バイトを16進表示する | `55 48 89 e5 eb 00 fa f4 eb fc` |
| `cmp` | ビルド結果とQEMUが読むファイルを比較する | 無出力、終了コード0。不一致ならここで止める |

現在のProgram Header：

| 項目 | 値 |
|---|---|
| PT_LOAD数 | 1 |
| Offset | `0x1000` |
| VirtAddr / PhysAddr | ともに `0x100000` |
| FileSiz / MemSiz | ともに `0xA` |
| Flags / Align | R E / `0x1000` |

値が異なる場合、以下のウォッチポイントのアドレス・期待値をそのまま使わない。新しいPT_LOADの末尾と内容に合わせる必要がある。

### Bootloaderの停止位置もファイルから確認する

```sh
objdump -x esp/EFI/BOOT/BOOTX64.EFI
objdump -d esp/EFI/BOOT/BOOTX64.EFI > build/bootloader-disassembly.txt
objdump -s -j .rdata esp/EFI/BOOT/BOOTX64.EFI > build/bootloader-rdata.txt
less build/bootloader-disassembly.txt
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `objdump -x` | PEのImageBaseやセクション情報を読む。実行時の再配置と区別する | 今回のImageBaseは `0x140000000`。シンボルはない |
| `objdump -d ... > ...` | 命令列を保存する。コピー呼び出しの復帰先を照合する | 下記の命令列が見つかる |
| `objdump -s -j .rdata` | UTF-16の文字列データを保存する。成功メッセージの参照先を確認する | 今回は `0x14000373e` から成功メッセージがある |
| `less` | 保存した逆アセンブルを閲覧する | `/140001fe6` 等で検索できる。`q`で閲覧を終える |

今回のバイナリでは次の対応になる。RVAは「表示アドレス − ImageBase」で計算する。

```text
140001fbf: mov 0x160(%rax),%rax       # Boot ServicesのCopyMem
...                                  # RCX=配置先、RDX=コピー元、R8=長さ
140001fe6: call *%rax
140001fe8: mov 0x138(%rsp),%rax       # CopyMemの復帰先：RVA 0x1fe8
...
140002146: jmp 0x140001f4d            # セグメントのループへ戻る
14000214b: ...                        # ループを抜けた位置
140002167: lea ...,%rdx              # 0x14000373eの成功メッセージ
14000216e: call *%rax                 # OutputString
140002170: movq $0x0,0xa8(%rsp)      # 出力から戻った位置：RVA 0x2170
```

`bootloader/main.c`のCopyMem呼び出し、ループ終了、成功メッセージ出力の順序とも照合する。**ImageBase・RVA・文字列アドレスは再ビルドで変わり得る。上記と違ったら、新しい逆アセンブルで特定し直す。** ELFのKernelシンボルを読み込んでも、PE形式のBootloaderの関数に名前でブレークできるようにはならない。

## 2. ターミナル1：QEMUを起動する

通常起動は `make run`。今回の検証では、通常起動のQEMUを終了してから次を実行する。

```sh
make debug
```

ビルド後、通常のQEMU構成に `-S -gdb tcp:127.0.0.1:1234` を追加して起動する。`-S`はCPUを起動直後に停止し、`-gdb`はGDBの接続を待ち受ける。GDBで監視を設定する前にロードが終わるのを防ぐために必要となる。

成功時はQEMUが起動して待機する。画面がまだ進まなくても正常。ターミナル1はそのままにしておく。通常起動と同じESP・`build/OVMF_VARS.fd`を使用し、実行中はこれらを他のQEMUと共有しない。以降、QEMUを終了するまで再ビルドやESPの書き換えをしない。

## 3. ターミナル2：GDB接続と監視の設定

```sh
cd ~/workspace/jdy-os
gdb -q build/kernel.elf
```

`gdb -q`は起動メッセージを省略し、KernelのELFとシンボルを読む。成功時は `Reading symbols from build/kernel.elf` とGDBプロンプトが出る。

以下はGDBの中で実行する。

```gdb
set pagination off
set logging file build/step2-3-gdb.log
set logging overwrite on
set logging enabled on
target remote localhost:1234
watch *(unsigned short *)0x100008
info breakpoints
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `set pagination off` | 長い出力のページ待ちをなくす | 出力が途中で止まらない |
| `set logging file` | 操作結果の保存先を決める | 指定先で後からログを読める |
| `set logging overwrite on` | 前回のログを上書きする | 今回の結果だけを記録できる |
| `set logging enabled on` | ログ記録を始める | 保存先の案内が表示される |
| `target remote` | QEMUのCPUへ接続する | `Remote debugging using localhost:1234`。初期PCは通常 `0xfff0` |
| `watch` | 最後の2バイトの書き込みを監視する | `Hardware watchpoint N` が表示される |
| `info breakpoints` | 監視番号と有効状態を確認する | 今作った監視が有効になっている |

最初の監視番号が1なら以下を実行する。番号が違う場合は1を読み替える。

```gdb
condition 1 *(unsigned short *)0x100008 == 0xfceb && *(unsigned long long *)0x100000 == 0xf4fa00ebe5894855
continue
```

`condition`は期待する10バイトが揃った場合だけ停止させる。ファームウェアが先に同じアドレスを書き換える場合を除外するために必要。little endianなので、先頭8バイト `55 48 89 e5 eb 00 fa f4` は整数で `0xf4fa00ebe5894855`、末尾 `eb fc` は `0xfceb` になる。

`continue`はCPUを再開する。成功時は起動後にウォッチポイントで停止し、末尾の新しい値が64747（`0xfceb`）になる。数十秒かかる場合がある。

**ここではまだCopyMem内で停止している可能性がある。これだけで「全PT_LOAD処理完了」と判断しない。** `break KernelMain`も今回の目的には使えない。Step 2-3はKernelへジャンプしないため到達しない。

## 4. コピー関数から戻り、全ロード完了位置で停止する

```gdb
x/12i $pc
info registers rip rsp rdi rsi rcx rdx r8
x/8gx $rsp
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `x/12i $pc` | 現在位置から12命令を読む。コピーの途中か復帰直前かを判断する | 今回の環境では下の4命令から始まる |
| `info registers ...` | CPUレジスタを読む。命令位置・スタック・コピー進行を確認する | 今回はRDI=`0x10000a`、RCX=0。これだけを完了証明にはしない |
| `x/8gx $rsp` | スタックから8個の64ビット値を表示する | 次のpopやretが読む値を確認できる |

以前の検証では停止時の命令列は次だった。

```asm
cld
pop %rdi
pop %rsi
ret
```

**実際にこの4命令が並んでいる場合だけ**、次を実行する。

```gdb
disable 1
stepi 4
x/8i $pc
```

`disable 1`は捕捉済みの監視を無効にする。`stepi 4`（短縮形 `si 4`）は機械語命令を4つ実行し、CopyMemの呼び出し元へ戻る。成功時はPCの命令列が、先に調べたPEのRVA `0x1fe8` の命令列と一致する。

異なる命令列なら `stepi` と `x/i $pc` を1命令ずつ交互に実行してコピー関数の復帰を確認する。`stepi`は実際にCPUを動かす操作なので、回数を推測して入力しない。シンボルのない `bt` や `finish` だけに依存せず、retの復帰先と静的逆アセンブルを照合する。追跡できない場合は後述の代替案を使う。

復帰先を確認できたら、今回のRVAに対して次を実行する。

```gdb
set $loader_base = $pc - 0x1fe8
p/x $loader_base
x/2bx $loader_base
hbreak *($loader_base + 0x2170)
info breakpoints
continue
x/i $pc
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `set $loader_base` | 実PCから復帰先RVAを引き、実行時PEベースを求める | 以前の検証では `0x1de9b000`。この値自体は固定しない |
| `p/x` | 計算結果を16進表示する | 妥当なベースが得られる |
| `x/2bx` | PE先頭を読む。計算の補助確認をする | MZの `0x4d 0x5a`。命令列の一致も必須 |
| `hbreak *...` | 再配置後の成功メッセージ出力直後へハードウェアブレークを設定する | 計算したアドレスのブレークが表示される |
| `info breakpoints` | 新しい番号・アドレスを確認する | 目的の停止先が有効 |
| `continue` | 全セグメント処理と成功メッセージ出力を終えるまで進む | 新しいブレークポイントで停止し、QEMUに成功メッセージが表示される |
| `x/i $pc` | 最終停止命令を読む | 今回は `movq $0x0,0xa8(%rsp)`。静的逆アセンブルに一致 |

この位置なら全PT_LOADのコピー・必要なゼロ初期化を終えている。まだKernelは実行していない。QEMUは停止したままなので、既存の5秒待機を気にせず調査できる。

## 5. メモリと機械語を確認する

```gdb
x/10xb 0x100000
x/6i 0x100000
disassemble /r KernelMain
monitor xp /10bx 0x100000
dump binary memory build/step2-3-loaded.bin 0x100000 0x10000a
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `x/10xb` | 10個のバイトを16進数で表示する。配置内容を直接確かめる | 下記の10バイト |
| `x/6i` | メモリを6命令として逆アセンブルする | 下記の6命令 |
| `disassemble /r KernelMain` | KernelMainを命令バイト付きで表示する。ファイルの逆アセンブルと比べる | 同じバイトと命令 |
| `monitor xp` | QEMUモニター経由で物理メモリを読む。物理配置先も確認する | GDBの表示と同じ10バイト |
| `dump binary memory` | 読んだメモリをファイル保存する。目視以外でも比較できるようにする | 10バイトのファイルができる。終端アドレスは含まない |

```text
55 48 89 e5 eb 00 fa f4 eb fc
```

```asm
0x100000: push %rbp
0x100001: mov  %rsp,%rbp
0x100004: jmp  0x100006
0x100006: cli
0x100007: hlt
0x100008: jmp  0x100006
```

`x`は `x/個数・表示形式・単位 アドレス` で指定する。`x`が16進形式、`b`が1バイト、`g`が8バイト、`i`が命令表示。逆アセンブルは命令を読む操作であり、Kernelの命令を実行する操作ではない。

ファイルとの比較は、GDB内からシェルコマンドを呼ぶか、空いているターミナルで行う。以下はGDB内用。

```gdb
shell dd if=build/kernel.elf of=build/step2-3-expected.bin bs=1 skip=4096 count=10 status=none
shell od -An -tx1 build/step2-3-expected.bin
shell od -An -tx1 build/step2-3-loaded.bin
shell cmp build/step2-3-expected.bin build/step2-3-loaded.bin && echo PASS: exact-byte-match
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `shell dd` | ELFの `0x1000` から10バイトだけ抽出する。ヘッダーを比較しないため | expected.binが10バイトになる |
| `shell od` | コピー元とコピー先をそれぞれ16進表示する | 両方とも期待する10バイト |
| `shell cmp ... && echo ...` | ファイルをバイト単位で比較する | `PASS: exact-byte-match`。不一致ならPASSは出ない |

## 6. BSSのゼロ初期化を別途検証する方法

現在は `p_memsz == p_filesz == 10` なので、ゼロ化対象は0バイト。今回の一致だけでBSS処理まで検証したことにはならない。

追加検証では、通常のKernelとESPを保管したまま、別ディレクトリにテストKernelとESPを作る。例えば一時的なC++ソースに `volatile unsigned char bss_probe[16];` をグローバル定義して既存リンカスクリプトでリンクすると、BSSを持つELFを作れる。通常のソースは変更せず、一時ソースを用意する。

1. `readelf -l <テストELF>` で `p_memsz > p_filesz` のPT_LOADを確認する。`nm -n <テストELF>` で `bss_probe` のアドレスも記録する。成功条件はゼロ化対象が実際にセグメントに含まれていること。
2. ゼロ化対象を `[p_paddr + p_filesz, p_paddr + p_memsz)` と計算する。`.bss`という名前だけで範囲を決めない。
3. コピー捕捉からBootloaderベースを特定し、新しい逆アセンブルで対象セグメントの `SetMem` 呼び出し直前に `hbreak` を設定する。成功条件はまだSetMemを呼ぶ前に停止できること。複数回呼ばれる場合は引数で対象を確認する。
4. `info registers rcx rdx r8` で、UEFIのx64呼出規約に従うSetMemの引数（RCX=開始、RDX=長さ、R8の下位バイト=0）を確認する。
5. 対象がちょうど16バイトなら `set {unsigned char[16]} <開始アドレス> = {0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5}` でゲストメモリを非ゼロへ書き換える。`x/16xb <開始アドレス>` ですべてA5になったことを確認する。長さは実ELFに合わせる。これにより「もともとゼロだった」を除外する。
6. 全ロード完了位置まで `continue` し、同じ範囲を `x/16xb` で読む。全バイト0、ファイル由来部分は元の内容を維持していれば成功。

この操作は追加テストの設計であり、通常の10バイトKernelにそのまま適用しない。複数セグメントや新しいビルドでは、監視条件・RVA・比較範囲をすべて調べ直す必要がある。

## 7. 異常時の調べ方と代替案

| 症状 | 調べること・対処 | 解決したと判断する条件 |
|---|---|---|
| 接続できない | ターミナル1で `make debug` が起動中か確認。接続先はlocalhost:1234。ポート使用中なら既存のデバッグQEMUを終了する | Remote debuggingの表示が出る |
| OVMFやVARSがない | Makefileが指定する `/usr/share/OVMF/OVMF_CODE_4M.fd` と `build/OVMF_VARS.fd` の存在・対応サイズを確認する。VARSは対応するテンプレートから用意する | QEMUがドライブエラーなく起動する |
| 画面が進まない | 接続後に監視を設定し `continue` したか確認する | UEFIが起動する |
| 監視に到達しない | GDBでCtrl+Cを押して一時停止し、QEMUのエラー表示、ESPのKernel、AllocatePagesの成否、実ELFの末尾と監視条件を確認する | 再起動後に正しい条件で捕捉できる |
| `watch`がHardwareにならない | ハードウェア監視の対応と利用数を確認。不要な監視は `delete N` で削除する | Hardware watchpointとして作成できる |
| PEに名前でbreakできない | KernelのシンボルはBootloaderには使えない。上記の実アドレス指定を使う | 静的命令列と実メモリの命令列を照合できる |
| MZが見つからない／命令列が違う | 復帰先の特定かRVAが誤っている。計算を中断し、再ビルド後の逆アセンブルと照合する | ベースと命令列の両方が一致する |
| メモリが一致しない | 完了位置まで到達したか、比較対象とESPが同一か、Offset/PhysAddr/FileSizが正しいかを再確認する | 全バイト一致する。先頭がゼロだけでは成功としない |

Ctrl+CはGDBでCPUを一時停止する操作であり、QEMUを終了する操作ではない。`delete N`は指定番号の監視・ブレークを消すので、`info breakpoints`で番号を確認してから使う。

停止位置を確実に追えない場合の代替案は、成功メッセージ直後にデバッグ限定の待機を入れる方法。**このリポジトリには追加していない。別途変更内容を確認してから導入する。** 例えば専用マクロで囲んだ `for (;;) { __asm__ volatile("pause"); }` なら、最適化で消えない待機になる。成功表示後にGDBのCtrl+Cで停止してPCがそのループ内であることを確認する。再開だけではループを抜けないため、調査後はQEMUを終了し、マクロを外して再ビルドする。割り込み状態に依存する `hlt` より、調査用の待機として扱いやすい。Step 2-4のジャンプは追加しない。

## 8. 終了する

GDBで次を実行する。

```gdb
set logging enabled off
monitor quit
quit
```

| コマンド | 何をするか・なぜ必要か | 成功時に確認するもの |
|---|---|---|
| `set logging enabled off` | 記録を閉じる。ログを保存する | build/step2-3-gdb.logを読める |
| `monitor quit` | QEMUを終了する。停止中のVMも残さない | QEMU画面が閉じ、ターミナル1へ戻る |
| `quit` | GDBを終了する | ターミナル2へ戻る。確認が出たら `y` |

QEMU終了に伴う接続切断メッセージは正常。別の終え方はQEMU画面を閉じること。`detach`はデバッグ接続を外してQEMUを再開するため、終了目的では `monitor quit` を使う。

## 結果記録用チェックリスト

- 日付：
- 使用したGitブランチ・作業中の変更：
- 使用したELF：
- Entry / KernelMain：
- PT_LOADのOffset / PhysAddr / FileSiz / MemSiz：
- BootloaderのImageBase / CopyMem復帰先RVA / 完了位置RVA：
- 実行時Bootloaderベース / 最終停止PC：
- ELFのバイト列：
- GDBメモリのバイト列：
- 不一致・気づいた点：

- [ ] ビルド結果とESPのKernelが一致した
- [ ] QEMUを `make debug` で起動した
- [ ] localhost:1234へGDBで接続した
- [ ] ELFに合う条件でハードウェアウォッチポイントを設定した
- [ ] コピー関数からの復帰を命令列で確認した
- [ ] PE再配置を考慮して完了位置のブレークを設定した
- [ ] 全PT_LOAD処理後の停止を確認した
- [ ] メモリ10バイトとELFの10バイトが完全一致した
- [ ] 物理メモリ表示も一致した
- [ ] 逆アセンブルがKernelMainの全6命令と一致した
- [ ] 現在のELFにはBSSゼロ化対象がないことを確認した
- [ ] 追加のBSS検証をした／未実施を記録した
- [ ] ログを保存しGDBとQEMUを終了した

現在のELFのコピー検証が成功しても、BSS・複数セグメント・異常系のすべてを実機検証したことにはならない。確認できた範囲を上の記録に残す。
