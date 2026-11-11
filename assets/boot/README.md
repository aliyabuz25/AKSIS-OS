# Açılış logosu

`dragon.png`, kullanıcının sağladığı 1536×1024 mavi ejderha görselinin
değiştirilmemiş kaynağıdır. SHA-256:
`331ab8d4fff75f6c13f48fd394ab00549879f81acfbd9673e1ca009aa69c22c1`

`tools/build-boot-logo.py` görüntüyü kayıpsız RGB24 verisine dönüştürür.
Veri kernel'in salt okunur bölümüne gömülür; açılış sırasında diskten resim
okunmaz. `boot_logo_draw()` en-boy oranını korur ve yalnızca açılışta bir kez
bilinear ölçekleme yapar. Orijinal renkler RGB565'e indirgenmez.
