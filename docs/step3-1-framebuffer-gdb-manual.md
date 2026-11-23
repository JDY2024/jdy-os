# Step 3-1 GOPとFramebufferの描画をGDBで確認する

## 起動して画面を見る

```sh
make
make run
```

ルートの `Makefile` は `bootloader/Makefile` で UEFI アプリケーションを、`kernel/Makefile` でデバッグ情報付きの ELF64 カーネルを作ります。QEMU は ESP から `BOOTX64.EFI` を起動します。カーネルに到達すると濃紺の背景に次の3行が表示され、その下に赤、緑、青の矩形が並びます。

```text
JDY-OS
Hello from Kernel!
Framebuffer: 1280x800
```

`1280x800` は検証時の値です。`KernelMain()` は GOP の現在のモードから渡された幅と高さを `AppendDecimal()` で10進文字列に変換します。

ウィンドウを開けない環境では、QEMU モニターから仮想画面を PPM ファイルに保存できます。モニターのプロンプトが出たら、OSの起動を待って以下のコマンドを実行します。

```sh
TMPDIR=/tmp make run QEMU='qemu-system-x86_64 -display none -monitor stdio -no-reboot -d cpu_reset,int -D build/step3-1-qemu.log'
```

```text
(qemu) screendump build/step3-1.ppm
(qemu) info registers
(qemu) quit
```

`screendump` は QEMU の表示内容を保存します。`info registers` の `HLT=1` と `Halt()` 内の RIP は、描画を終えたCPUが `hlt` で停止していることを示します。

## GOPとFramebuffer

GOP（Graphics Output Protocol）は、UEFI が提供する画面モードの取得、設定、ブロック転送のためのインターフェースです。Framebuffer は画面に対応するピクセルを保持するアドレス領域です。Bootloader が GOP から場所と配置を調べ、カーネルがそのアドレスに CPU のストア命令で色を書き込むと、QEMU の仮想ビデオデバイスが書き込まれたピクセルを表示します。

| GOP の値 | 意味 |
| --- | --- |
| `FrameBufferBase` | Framebuffer の先頭物理アドレス |
| `FrameBufferSize` | Framebuffer 領域のサイズ（バイト） |
| `HorizontalResolution` | 画面に見える1行のピクセル数 |
| `VerticalResolution` | 画面に見える行数 |
| `PixelsPerScanLine` | 隣の行の先頭までの距離（ピクセル単位の stride）。行末にパディングがあると画面幅より大きい |
| `PixelFormat` | 1ピクセルの色成分の並び |

対応モードでは1ピクセルは4バイトです。先頭アドレスから昇順に RGB は `赤 緑 青 予約`、BGR は `青 緑 赤 予約` と並びます。例えば RGB 値 `(16, 24, 40)` の背景は、RGB の場合 `10 18 28 00`、BGR の場合 `28 18 10 00` と見えます。予約バイトには0を書きます。`PixelBitMask` は色成分の抽出と格納に `PixelInformation` のマスクが必要です。この実装は `PixelBitMask` と、直接描画用の領域を提供しない `PixelBltOnly` を明示的に拒否します。

## BootloaderからKernelへの受け渡し

1. `bootloader/main.c` の `get_framebuffer()` が `LocateProtocol()` で GOP を取得し、現在モードを調べます。幅、高さ、stride、ベース、サイズを検証し、最後の可視ピクセルの末尾 `4 * ((height - 1) * stride + width)` が領域内に収まることを乗算前に確認します。
2. `framebuffer_is_mapped()` が `CR3` を起点に現在のページテーブルをたどります。各階層で Present と Writable を検査し、可視範囲を覆う各ページについて仮想アドレスと物理アドレスが一致することを確認します。テーブルが置かれたRAMにも同一アドレスでアクセスできる OVMF の構成を前提にしています。この検査で、新しいページテーブルを作らずに GPU の物理アドレスをポインタとして使えることが分かります。
3. 取得した値は、Bootloader イメージの静的領域にある `framebuffer_config` に保存します。`include/framebuffer_config.h` で C と C++ に同じ型を定義し、サイズ32バイト、アラインメント8バイト、各メンバーのオフセットを両方のビルドで検証します。この段階のカーネルは Bootloader の領域を再利用しないため、このデータは停止まで有効です。
4. `boot_kernel()` が最新のメモリマップとキーを取得し、`ExitBootServices()` を呼びます。成功すると `cli` でマスク可能な割り込みを無効にし、`cld` で方向フラグをクリアして、`KernelEntry` 型で `KernelMain(const FramebufferConfig*)` を呼びます。CPUは既存のスタックとページテーブルを引き継ぎます。SysV AMD64 ABI の第1引数は `RDI` に入り、`call` が8バイトの戻り先を積むため、関数入口では `RSP & 0xf` が8になります。カーネルは以後、Framebuffer を直接更新します。

Bootloader は Windows ターゲットでビルドしているため、通常の呼び出しでは第1引数は `RCX` に入ります。GNU-EFI ヘッダーはこの構成で `__attribute__` をマクロで無効化するため、`sysv_abi` を使う共通ヘッダーを先にインクルードします。

## ピクセルと文字の描画

`kernel/main.cpp` の `PixelWriter::WritePixel()` は、座標が可視範囲内か、色を格納する4バイトが領域内かを順に検査します。書き込み先の計算式は `base + 4 * (y * stride + x)` です。例えば幅800で stride が832なら、2行目の先頭は `base + 4 * 832` です。`volatile` ポインタを介して4バイトを書き、デバイスへのストアが最適化で消えないようにします。

`PixelWriter::FillRect()` は矩形を画面境界で切り詰めてから、各ピクセルを描きます。背景も画面全体を覆う矩形です。色や文字を変更する場合は `KernelMain()` の描画呼び出しを編集します。

`kernel/font.h` のフォントは jdy-os 用に作成したオリジナルデータで、CC0-1.0 です。`Glyph()` が英大文字・小文字、数字、空白、コロン、ハイフン、感嘆符の字形を選び、その他の文字は疑問符で表示します。`GlyphRow()` は圧縮して格納した5×7の字形を上下の余白と行の反復で8×16のセルに展開します。`PixelWriter::DrawChar()` は行のビットを上位から順に見て、1の位置だけ前景色を書きます。`PixelWriter::DrawString()` は1文字ごとに8ピクセル右へ進みます。改行したい場合は、`KernelMain()` のように行ごとに座標を指定します。

## GDBで入口と描画後の値を比べる

端末1で `make debug` を実行します。ウィンドウが使えない場合は次のコマンドを使います。QEMU はリセット直後に停止して GDB を待ちます。

```sh
TMPDIR=/tmp make debug QEMU='qemu-system-x86_64 -display none -no-reboot -d cpu_reset,int -D build/step3-1-qemu.log'
```

端末2で `gdb -q build/kernel.elf` を実行し、以下を入力します。

```gdb
set architecture i386:x86-64
target remote 127.0.0.1:1234
hbreak *KernelMain
continue
info registers rip rdi rsp eflags
p/x ((unsigned long long)$rsp & 0xf)
set $cfg = (FramebufferConfig *)$rdi
p *$cfg
p/x $cfg->frame_buffer_base
set $fb = (unsigned char *)$cfg->frame_buffer_base
set $stride = $cfg->pixels_per_scan_line
x/4bx $fb
x/4bx $fb + 4 * (108 * $stride + 24)
```

`hbreak *KernelMain` は ELF ロード前にも設定でき、プロローグ前で停止します。この時点では GDB が表示するソース変数 `config` の値は未確定なので、実レジスタ `$rdi` を読みます。`RIP` は `0x100000`、`RSP & 0xf` は `0x8` が期待値です。検証時はベース `0x80000000`、サイズ `4096000` バイト、幅 `1280`、高さ `800`、stride `1280`、形式 `1`（BGR）でした。先頭ピクセルと赤い矩形の先頭位置は描画前にどちらも `00 00 00 00` でした。描画前の値はファームウェアが表示した内容で変わります。

```gdb
hbreak Halt
continue
x/4bx $fb
x/4bx $fb + 4 * (108 * $stride + 24)
x/4bx $fb + 4 * (108 * $stride + 64)
x/4bx $fb + 4 * (108 * $stride + 104)
x/4bx $fb + 4 * ($stride * ($cfg->vertical_resolution - 1) + $cfg->horizontal_resolution - 1)
monitor screendump build/step3-1.ppm
```

BGR モードでの期待値は順に、背景 `28 18 10 00`、赤 `6e 64 ff 00`、緑 `98 da 5c 00`、青 `ff a4 5c 00`、右下の背景 `28 18 10 00` です。終了ループまで実行してから GDB の Ctrl+C で停止し、`monitor info registers` で `HLT=1` も確認できます。

## 画面が表示されない場合

1. `make` で ELF と ESP を更新します。ウィンドウが開けない場合は `-display none` と `screendump` で仮想画面を確認します。FAT ドライブ用一時ファイルを作成できない場合は `TMPDIR` に書き込めるディレクトリを指定します。
2. 画面に `LocateProtocol(GOP) failed`、`Unsupported GOP pixel format`、`Invalid GOP framebuffer range`、またはマッピングエラーが出た場合は、表示された段階で Bootloader が停止しています。QEMU のビデオデバイスと OVMF の組み合わせを確認します。
3. `KernelMain` に到達しない場合は、画面上の ELF ロードのログを確認し、`readelf -hW build/kernel.elf` でエントリアドレスを照合します。到達した場合は `RDI` と `RSP` を確認し、`Halt` で先頭ピクセルの値を比較します。先頭は更新されるのに2行目以降がずれる場合は、アドレス計算に幅ではなく stride が使われていることを確認します。
4. QEMU が再起動したり終了したりする場合は、`-no-reboot` と `-d cpu_reset,int` を付けて、ログに初期リセット以降の `CPU Reset`、`Triple fault`、ページフォルト（`v=0e`）がないか確認します。ページフォルトの場合は `monitor info registers` で `CR2` を見て、Framebuffer のアドレス範囲や `CR3` のページテーブルを調べます。
