<div align="center">

# 🌌 AKSIS OS (SHENLONG Engine)

### **Modern, High-Performance x86_64 Microkernel Operating System & Graphical Desktop Environment**

[![Architecture: x86_64](https://img.shields.io/badge/Architecture-x86__64-blue.svg)](#mimari-ve-teknik-altyapı)
[![Boot: Limine UEFI / BIOS](https://img.shields.io/badge/Bootloader-Limine%20UEFI%20%2F%20BIOS-orange.svg)](#önyükleme-ve-uyumluluk)
[![Display: 1080p DWM Compositor](https://img.shields.io/badge/Display-1080p%20FHD%20%7C%20DWM%20UI-success.svg)](#grafik-arayüz-ve-masaüstü-deneyimi)
[![Networking: Native TCP/IP & TLS](https://img.shields.io/badge/Network-RTL8139%20%7C%20TCP%2FIP%20%7C%20BearSSL-purple.svg)](#ağ-yığını-ve-web-motoru)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-green.svg)](LICENSE)

<br/>

**AKSIS OS**, sıfırdan (*from scratch*) saf C ve Assembly dilleriyle geliştirilmiş; modern x86_64 Long Mode üzerinde koşan, Limine bootloader ile UEFI ve Legacy BIOS desteği sunan, çift tamponlu (double-buffered) donanım ivmeli kompozitör pencere yöneticisi ve yerel web çalışma zamanına sahip yeni nesil bir işletim sistemidir.

[Özellikler](#-öne-çıkan-özellikler) • [Mimari](#-mimari-ve-teknik-altyapı) • [Masaüstü & Arayüz](#-grafik-arayüz-ve-masaüstü-deneyimi) • [Web & Ağ](#-ağ-yığını-ve-web-motoru) • [Kurulum & Derleme](#-derleme-ve-çalıştırma) • [Kullanım](#-kullanım-ve-kısayollar)

</div>

---

## 🌟 Öne Çıkan Özellikler

- **Çift Modlu Hibrit Çekirdek:** 64-bit Long Mode tam bellek yönetimi (4-Level Paging, PAT - Page Attribute Table ile hızlı GPU video bellek yazımı).
- **Evrensel Önyükleme (UEFI + BIOS):** Limine boot protokolü ile donanım düzeyinde doğrudan UEFI 64-bit ve x86 BIOS uyumluluğu.
- **DWM & WDDM Tabanlı Pencere Yöneticisi:** Windows 10 tarzı fluent / retained UI, taşınabilir, küçültülebilir, büyütülebilir çift tamponlu pencereler ve 100 Hz pacing donanım kompozitörü.
- **Native AXIS Web Engine:** Standart C99 Lexbor (HTML5 / DOM parser) ve gömülü QuickJS (ES2020) çalışma zamanı ile çalışan gerçek DOM/CSS/JS web motoru.
- **Tam Ağ Yığını (TCP/IP & TLS 1.3):** RTL8139 ağ kartı sürücüsü, ARP, IPv4, ICMP, DHCP, DNS, TCP soketleri ve `BearSSL` ile HTTPS/TLS desteği.
- **Yerel AI Asistanı (Axis Copilot):** İşletim sistemine entegre edilmiş, sistem komutlarını ve görevlerini yönetebilen yerel asistan paneli.
- **POSIX & Dosya Sistemi (AXISFS):** IDE/ATA sürücüsü üzerinden kalıcı disk desteği (`/dev/ide0`), Unix izin modeli (0755, 0777 vb.) ve VFS (Sanal Dosya Sistemi).
- **Yüksek Kaliteli Medya & Font Motoru:** MPEG-1 video oynatıcı (`pl_mpeg`), Selawik/Noto Sans Mono anti-aliased bitmap font işleyici ve kayıpsız vektörel ikon çizimi.

---

## 🏗 Mimari ve Teknik Altyapı

```
+-------------------------------------------------------------------------+
|                              AKSIS DESKTOP                              |
|   +------------------+  +-------------------+  +--------------------+   |
|   |  Terminal (Bash) |  |   AXIS Web / JS   |  |  Sistem & Ayarlar  |   |
|   +------------------+  +-------------------+  +--------------------+   |
|   |  Dosya Yöneticisi|  |   MPEG Player     |  |  AI Assistant      |   |
|   +------------------+  +-------------------+  +--------------------+   |
+-------------------------------------------------------------------------+
|                  WDDM / DWM Compositor & 2D GDI Subsystem               |
+-------------------------------------------------------------------------+
|                     VFS (Virtual File System) & ATA Driver              |
|                     Network Stack (RTL8139 / TCP / TLS)                 |
+-------------------------------------------------------------------------+
|                           AKSIS x86_64 KERNEL                           |
|       [Interrupts (IDT/PIC)]  [Memory (Paging/PAT)]  [Process/Sched]    |
+-------------------------------------------------------------------------+
|                  Limine Bootloader (UEFI / x86_64 BIOS)                 |
+-------------------------------------------------------------------------+
|                                HARDWARE                                 |
+-------------------------------------------------------------------------+
```

### 1. Çekirdek & Bellek Yönetimi
- **Giriş ve Mod:** x86_64 Long Mode, IDT (Interrupt Descriptor Table), PIC, PS/2 klavye ve fare sürücüleri.
- **Zamanlama:** TSC (Time Stamp Counter) yüksek hassasiyetli saat ve RTC fallback mekanizması.
- **PAT (Page Attribute Table):** Framebuffer erişiminde Write-Combining etkinleştirilerek 4K/1080p çizim performansı maksimize edilmiştir.

### 2. Sanal Dosya Sistemi (VFS & Kalıcı Disk)
- Unix benzeri hiyerarşi: `/tmp` (777 tmpfs), `/sys` (donanım/uptime metrikleri), `/dev`, `/bin`.
- Sabit disk desteği: ATA / IDE PIO mod sürücüsü ile disk blokları üzerine kalıcı dosya sistemi.

---

## 🎨 Grafik Arayüz ve Masaüstü Deneyimi

AKSIS OS, metin tabanlı basit kabukların ötesinde zengin bir grafik masaüstü ortamı sunar:

- **1920×1080 Full HD Doğal Çözünürlük:** Ekran kartının GOP (Graphics Output Protocol) veya VBE modlarına göre otomatik ölçeklenir.
- **Fluent & DWM Pencere Yöneticisi:**
  - Saydam başlık çubukları, alfa kanallı gölgelendirmeler.
  - Yumuşatılmış (anti-aliased) 4×4 örneklemeli vektörel imleç ve UI kontrolleri.
  - Windows tuşu ile açılan, arama özellikli ortalanmış Başlat Menüsü.
  - Çoklu pencere odağı, minimize/maximize ve sürükle-bırak desteği.
- **Zengin Yerleşik Uygulamalar:**
  - **Terminal:** UTF-8 Türkçe Q klavye destekli, Noto Sans Mono tabanlı sabit genişlikli kabuk.
  - **AXIS Web:** Dahili HTML/CSS/JS tarayıcı arayüzü.
  - **Medya Oynatıcı:** Ses ve video akışını doğrudan işleyen MPEG motoru.
  - **Sistem Monitörü & Ağ Yöneticisi:** IP/DHCP durumu, bellek tüketimi ve canlı saat dilimi (UTC+3 İstanbul varsayılan).

---

## 🌐 Ağ Yığını ve Web Motoru

İşletim sistemi, modern web teknolojilerini native çekirdek üzerinde koşturacak şekilde tasarlanmıştır:

| Katman | Teknoloji / Kütüphane | İşlev |
|---|---|---|
| **Ağ Sürücüsü** | RTL8139 PCI NIC | Ham Ethernet paket alma/gönderme |
| **Protokoller** | ARP, IPv4, ICMP, DHCP | IP adresleme ve yerel ağ yönlendirmesi |
| **Uçtan Uca** | TCP / UDP Socket API | Güvenilir akış ve bağlantı yönetimi |
| **Şifreleme** | BearSSL (TLS 1.3/1.2) | HTTPS istemci bağlantıları ve sertifika doğrulama |
| **HTML/CSS Parser**| Lexbor (C99) | Standartlara tam uyumlu HTML5 DOM ve CSS ayrıştırma |
| **JS Engine** | QuickJS (ES2020) | DOM API köprüsü, dinamik script ve sayaç desteği |
| **Uzak Erişim** | Native SSH Server | SSH-2.0 protokolü ile uzaktan güvenli terminal oturumu |

---

## 🚀 Derleme ve Çalıştırma

### Gereksinimler

- **Derleyici & Araçlar:** `gcc` veya `clang`, `nasm`, `make`, `xorriso`, `mtools`
- **Sanal Makine (Test için):** `qemu-system-x86_64` veya [UTM](https://mac.getutm.app/) / [Parallels Desktop](https://www.parallels.com/)

macOS ortamında gerekli bağımlılıkları tek komutla kurmak için:
```bash
brew install nasm qemu xorriso mtools
```

### Derleme Adımları

1. **Depoyu klonlayın:**
   ```bash
   git clone https://github.com/aliyabuz25/AKSIS-OS.git
   cd AKSIS-OS
   ```

2. **Önyüklenebilir Hibrit ISO İmajını Oluşturun:**
   ```bash
   make iso
   ```
   *Bu komut bootloader, çekirdek, fontlar, medya varlıkları ve web motorunu derleyerek `build/out/shenlong.iso` (veya `axis.iso`) çıktısını üretir.*

3. **QEMU ile Doğrudan Başlatın:**
   ```bash
   make run
   # veya UEFI modunda:
   make run-uefi
   ```

4. **Birim Testlerini ve Arayüz Doğrulamasını Çalıştırın:**
   ```bash
   make test
   ```

---

## ⌨️ Kullanım ve Kısayollar

- <kbd>Win</kbd> : Başlat Menüsünü açar/kapatır.
- <kbd>Esc</kbd> : Açık menüleri, takvimi veya arama panellerini kapatır.
- <kbd>V</kbd> : Açılış logosu esnasında detaylı donanım/ağ log konsoluna geçiş yapar.
- <kbd>Enter</kbd> / <kbd>Esc</kbd> : Açılış animasyonunu atlar.
- **Türkçe Q Klavye:** İ, ı, Ğ, ğ, Ü, ü, Ş, ş, Ö, ö, Ç, ç karakterleri tam UTF-8 desteğiyle Terminal ve Web alanında işlenir.

---

## 📂 Dizin Yapısı

```
AKSIS-OS/
├── assets/          # Duvar kağıtları, TrueType/Bitmap fontlar, boot logoları, UI HTML şablonları
├── bootloader/      # MBR, Stage2 ve CD boot asm kaynakları
├── build/           # Limine yapılandırması, linker scripti ve derleme yardımcıları
├── docs/            # Web motoru, arayüz mimarisi ve teknik dokümantasyon
├── kernel/          # Çekirdek kaynak kodları (Bellek, IDT, VFS, GPU, DWM, TCP/IP, UI, Sürücüler)
│   ├── axis_web/    # Lexbor & QuickJS DOM çalışma zamanı ve bağlayıcıları
│   └── libc/        # Çekirdek içi bağımsız standart C başlıkları
├── limine/          # Limine UEFI & BIOS önyükleyici ikilileri
├── shell/           # Kullanıcı modu kabuk uygulamaları
├── tools/           # UI font derleyicileri, test scriptleri, UTM paketleyiciler
└── vendor/          # Lexbor, QuickJS, BearSSL ve gömülü kütüphaneler
```

---

## 📜 Lisans

Bu proje [GNU General Public License v3.0](LICENSE) kapsamında lisanslanmıştır. Detaylar için `LICENSE` dosyasına göz atabilirsiniz.
EOF