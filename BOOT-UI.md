# SHENLONG Light v0.7

`SHENLONG-v0.7-light.utm` paketi kendi ISO'sunu içerir. UTM ile açıp
**SHENLONG v0.7 - Light** makinesini soğuk başlatın. Önceki makinenin
askıya alınmış oturumu yeni kernel'i yüklemez.

Mevcut makine kullanılacaksa tamamen kapatın, CD/DVD medyasını yeni
`shenlong.iso` olarak seçin. x86_64 / Q35, 256 MB veya daha fazla RAM,
UEFI, virtio-vga, CPU `max`, AC97 ses, PS/2 controller açık ve USB input kapalı olmalıdır.
Host interneti için RTL8139 ve Emulated NAT gerekir; ayrıntılar [NETWORK.md](NETWORK.md).

## Açılış düzeltmesi

`iso9660_read -> init_config_disk -> _menu` izi Limine önyükleyicisine
aittir; kernel başlamadan yapılandırma okunurken oluşur. Önceki kaynak
ağacında bu fonksiyonların bulunmaması eski ISO kullanıldığını kanıtlamaz:
fonksiyonlar projedeki hazır Limine EFI dosyalarında bulunur.

Yeni ISO'nun EFI FAT bölümünde önyükleyici, `EFI/BOOT/limine.conf` ve
`boot/shenlong.elf` birlikte bulunur. UEFI yapılandırma ve kernel okumak
için ISO9660'a geri dönmez. BIOS için ISO9660 yolu korunur. ISO önce geçici
bir dosyada hazırlanır; BIOS kurulumu başarılı olursa son dosya atomik
olarak değiştirilir. Çalışan VM'nin açık medya dosyası yerinde kesilmez.

## Arayüz

Başlangıçta siyah zeminde ortalanmış mavi ejderha ve ince bir ilerleme
çubuğu görünür. Başlık, günlük metni, sayaç ve fare imleci varsayılan
görünümde yoktur. **V** ile gerçek firmware, bellek, ekran, giriş aygıtları
ve ağ günlüğüne geçilir; tekrar V sade görünüme döner. Logo, kaynak PNG'den
kayıpsız RGB24 olarak kernel'e gömülür; en-boy oranı korunur. Ekran ve masaüstü
1920×1080 (Full HD) ister; firmware'in desteklemediği makinelerde Limine daha
düşük bir moda dönebilir. Kernel, gerçek çözünürlüğü günlükte gösterir.

Logo varsayılan **18 saniye** görünür; Enter/Esc ile
erken geçilebilir. Süre TSC ile ölçülür; TSC bilgisi yoksa RTC kullanılır.
İki saat de okunamıyorsa bekleme atlanır. Firmware'in logo öncesindeki
hazırlık süresi ayrıca eklenir. İlerleme çubuğu yalnızca doluluk pikseli
değiştiğinde güncellenir. Logo görünüm değişince bir kez çizilir;
bekleme sırasında yeniden ölçeklenmez. Ağ günlüğü güncel tutulur.

Ardından açık gri masaüstü boş açılır. Terminal veya diğer uygulamalar otomatik
olarak açılmaz. Başlat menüsünden, masaüstü simgesinden ya da görev
dock'tan Terminal, Dosyalar, Sistem, Web, Ağ ve Medya açılabilir. Ortalanmış SHENLONG menüsü uygulama
araması, Enter ve yön tuşlarını destekler. Windows tuşu Başlat'ı açar,
Esc menü ve takvimi kapatır.

Tema açıktır, pencere çerçeveleri köşelidir. Parlak kenar ve bulanıklık yoktur.
Pencere başlıkları, menü ve takvim arka planı yaklaşık %6 saydamdır; uygulama
içerikleri ve metinler okunaklı kalır. İmlecin gövdesi siyah, siyah zeminde
görülebilmesi için dış çizgisi açıktır. İmleç ve kontrol köşeleri 4×4
kapsama örnekleriyle yumuşatılır. Özgün ikonlar dört kat çözünürlükte
çizilip yumuşatılarak önbelleğe alınır. Masaüstü simgeleri 64 px,
dock simgeleri 40 px boyutundadır. Alt dock ortalanmış 444×72 px bir yüzeydir;
ekran genişliğini kaplamaz. Ağ ve saat sağ üstte ayrı 236×56 px alandadır.

Pencereler taşınabilir, küçültülebilir, büyütülebilir ve kapatılabilir.
Dock, küçültülmüş pencereyi geri getirir.
Terminal içeriği taşıma ve boyut değişimlerinde korunur. Aynı uygulamanın
tek penceresi vardır; kapatıp açma mevcut oturum içeriğini geri getirir.
Dosyalar gerçek RAM dosya sistemini listeler; klasörlere girilebilir ve
dosyalar salt okunur önizlenebilir. Odaklanmamış terminale tuşlar gitmez.

Yumuşatılmış Türkçe UI yazıları Microsoft'un Segoe UI alternatifi Selawik'ten
üretilen Shenlong UI bitmap fontunu kullanır; Segoe UI'nin kendisi değildir.
Lisans ve kaynak `assets/fonts/` altındadır, ISO'ya da eklenir. Temel UI
yazısı 18 px'dir. Terminal, Noto Sans Mono'dan üretilmiş yumuşatılmış
10×24 hücreleri kullanır; komut hizalaması sabit genişlikli kalır.

UTM paketinde Retina görünümü ve Linear büyütme/küçültme filtresi seçilidir.
Konuk ekranı 1920×1080 kalır; yüksek çözünürlüklü pencerede bloklu büyütme
uygulanmaz. Bu ayar eski UTM paketlerine kendiliğinden aktarılmaz.

Klavye Türkçe Q'dur. Türkçe harfler, Shift/Caps Lock, AltGr ve tarayıcıda
Ctrl+L/Ctrl+A desteklenir. Terminal UTF-8 metni gösterir; yön tuşları,
Backspace ve Delete Türkçe harfleri tek karakter olarak işler.

Saat/tarih otomatik güncellenir. İlk zaman firmware'den, ilerleyen saniyeler
TSC'den gelir; TSC bilgisi yoksa RTC okunur. Varsayılan İstanbul (UTC+3),
Sistem penceresindeki saat dilimi düğmesiyle UTC'ye çevrilebilir. Saate
tıklayınca gerçek aya ve güne göre takvim açılır. Ağ zaman eşitlemesi yoktur.

Dosyalar RAM'dedir; yeniden başlatınca kaybolur. USB HID sürücüsü yoktur.
Full HD compositor ve altı pencere için 96 MB statik heap ayrılmıştır;
VM için en az 256 MB kullanın.

## Web ve medya

Yerel HTTPS, sertifika zinciri/alan adı/tarih doğrulamasıyla çalışır. Medya,
MPEG-1 video ve MP2 stereo sesi çekirdek içinde çözer; AC97 üzerinden çalar.
`video demo` gömülü örneği açar. Web'de `video/mpeg` türündeki doğrudan
bağlantılar Medya'ya aktarılır. İndirme 2 MiB, video 1280×720 ile sınırlıdır.
**YouTube oynatıcısı ve modern web uygulamaları çalışmaz:** JavaScript/DOM/CSS
motoru, H.264/VP9/AV1, AAC/Opus, akış ve DRM desteği yoktur. Google'ın HTML
metni okunabilir; arama formu henüz uygulanmamıştır. Ayrıntılar [NETWORK.md](NETWORK.md).

## Çizim performansı

Ekran yalnızca değişen dikdörtgenlerde birleştirilip framebuffer'a aktarılır.
İmleç hareketi önceki ve yeni imleç alanını günceller. Arka plan ve pencere
içerikleri önbellektedir; pencere taşıma sırasında metin ve simgeler yeniden
çizilmez. Opak alanlar toplu kopyalanır, yalnızca gereken yerlerde alpha
karışımı yapılır. Saatin saniye güncellemesi sağ üstteki küçük durum alanıyla sınırlıdır. Değişiklik yoksa framebuffer'a yazılmaz.

Odak veya pencere düğmesi hover durumu değişince yalnızca başlık alanı
yeniden çizilir; uygulama gövdesi korunur. Compositor pencerenin opak
gövdesini tam sınırlarıyla takip eder; başlığın saydamlığı yüzünden
yan ve alt şeritlerde gereksiz alpha hesabı yapmaz.

Aktarım sınırı TSC ile 120 Hz'dir; sınır nedeniyle ertelenen güncellemeler
kuyrukta kalır ve sonraki turda aktarılır. Açılış animasyonları 180 ms boyunca
zamana ve yumuşak yavaşlama eğrisine göre ilerler. Örtülen opak alanların
altındaki katmanlar çizilmez. Bu sınır ölçülmüş ekran FPS'i veya VSync garantisi
değildir: çizim yazılımsaldır ve hız kullanılan CPU/VM'ye bağlıdır. Giriş
aygıtları hâlâ polling ile okunur; boşta çizim yapılmaması CPU'nun tamamen
uyuduğu anlamına gelmez.

Terminalde `perf reset` sayaçları sıfırlar, `perf` aktarılan kare/piksel ve
ortalama/en uzun birleştirme süresini gösterir. Gerçek VM'deki imleç iş yükü
sonuçları test sonunda `build/out/*-performance.json` dosyalarına yazılır.

## Derleme ve doğrulama

Gerekenler: clang, nasm, x86_64-elf-ld, xorriso, mtools, ripgrep, Python 3.
Logo verisini üretmek, görsel testler ve fontu yeniden üretmek için Pillow gerekir. Saat birim
testi macOS'ta x86_64 olarak derlenir (Apple Silicon'da Rosetta ile çalışır).
macOS testleri kurulu `/Applications/UTM.app` içindeki QEMU ve firmware'i
kullanır; mevcut UTM makinelerini değiştirmez.

```sh
make iso
make test
make test-network
make test-native
python3 tools/package-utm.py build/SHENLONG-v0.7-light.utm
make desktop
```

Testler gerçek framebuffer'da Full HD çözünürlüğü, kaynak resimle logo
eşleşmesini, sade görünümü, V ile 11 günlük satırına gidip dönmeyi
ve 15–20 saniye açılış süresini doğrular.
`python3 tools/test-desktop.py --boot-only --skip-boot esc` ve `--skip-boot ret`
erken geçişi sınar. Süre, resim karşılaştırması ve ISO SHA-256 özeti
`build/out/*-boot-result*.json` dosyalarına yazılır.

`python3 tools/test-desktop.py --skip-boot esc --chrome-check`, başlık
düğmeleri üzerinde gezinirken terminal içeriğinin korunduğunu ve ortalama
hasarlı alanın pencerenin beşte birinden küçük kaldığını doğrular.
Çizim sayaçları `build/out/*-chrome-performance.json` dosyasına kaydedilir.

Diğer kontroller: boş masaüstü, canlı saat, Başlat ve
arama, klavye, kaydırma, taşıma, küçültme/büyütme, metni koruma, dosya
önizlemesi, odak, kapatma ve takvim. UEFI ve BIOS ayrı ayrı başlatılır.
Saatin gün/yıl/artık gün ve saat dilimi dönüşümleri ayrıca test edilir.
Türkçe Q tuşları ve HTTP/HTML ayrıştırıcıları da test edilir; ayrıştırıcı
testinde AddressSanitizer ve UndefinedBehaviorSanitizer kullanılır.
32 ve 24 bit compositor testleri bağımsız tam ekran referansıyla piksel
karşılaştırması yapar: şeffaflık, kırpma, örtüşme, gizleme, animasyon,
satır aralığı, boşta çizim yapmama ve ertelenmiş son karenin korunması.
Ekran görüntüleri `build/out/uefi-*.png` ve `build/out/bios-*.png` içindedir.

Limine yapılandırma arama sırası:
https://github.com/limine-bootloader/limine/blob/v12.x/CONFIG.md

UTM ekran ölçekleme ayarları:
https://github.com/utmapp/UTM/blob/main/Configuration/UTMQemuConfigurationDisplay.swift
