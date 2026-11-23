# BelleMiddleKey softkey kurulumu ve testi

Ayar uygulaması Qt 4.7.4 Widgets kullanır. Qt gerektirmeyen servis,
Window Server üzerinden fiziksel tuşları yakalar. Tam yakalamada motor,
tuşun bırakılmasını ve ardışık basışları 400 ms aralıkla değerlendirir.
Uzun basış eşiği 700 ms'dir. Yalnızca uzun basış atanan ve mantıksal kodu
bilinen tuşlarda `CaptureLongKey` kullanılır. Servis durdurulduğunda
yakalama kayıtları iptal edilir.

Kısayollar `C:\Data\BelleMiddleKey\kisayollar.txt` dosyasına kaydedilir.
Yeni eylemler `solsoftkey` ve `sagsoftkey` olarak yazılır. Mevcut dosya
biçimi, eylem adları ve varsayılan kısayollar korunur.

## Symbian olayları

- `e32keys.h` içinde `EKeyDevice0` ve `EKeyDevice1` mantıksal tuş
  kodlarıdır. `EStdKeyDevice0` ve `EStdKeyDevice1` sırasıyla `0xA4`
  ve `0xA5` tarama kodlarıdır. [Symbian e32keys.h](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/include/e32keys.h)
- İki eylem `RWsSession::SimulateKeyEvent` ile birer `TKeyEvent`
  gönderir. Window Server bu olayı sistemin tuş yönlendirmesine verir.
  Avkon komut düğmesi denetimi (`CEikCba`) `EEventKey` üzerinden
  işlem yapar. Bu yol klavye çeviricisine veya sentetik down/up çiftine
  ihtiyaç duymaz. Hedef uygulamanın ilgili softkey'i işlemesi gerekir.
  [Window Server API](https://katastrophos.net/symbian-dev/GUID-C6E5F800-0637-419E-8FE5-1EBB40E725AA/GUID-643DDA78-C7A7-386D-AB3F-8710141DDDA9.html)
  [Avkon kaynak kodu](https://github.com/SymbianSource/oss.FCL.sf.mw.classicui/blob/master/uifw/EikStd/coctlsrc/eikcba.cpp)
- `TRawEvent::Set(EKeyDown/EKeyUp, scan)` tarama koduyla çalışır.
  `UserSvr::AddEvent` bunu çekirdeğin ham olay kuyruğuna ekler.
  `RWsSession::SimulateRawEvent` Window Server'ın ham olay girişini
  kullanır. Ham yöntemler cihazın tarama kodu çevirisine bağlıdır.
  Menü ve görev değiştirici mevcut ham down/up yolunu kullanmaya devam eder.
  [TRawEvent tanımı](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/include/e32event.h)
  [UserSvr::AddEvent uygulaması](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/euser/us_exec.cpp)
- Window Server simülasyonu `SwEvent` ister. Proje bu yetkiyi zaten
  tuş işlemleri için kullanır. Servis tanımı uygulamanın mevcut yetki
  setini kullanır. `ReadDeviceData` ve `WriteDeviceData` profil
  işlemlerine, `PowerMgmt` uygulamaları kapatma işlevine aittir.
  [Window Server yetki denetimi](https://github.com/SymbianSource/oss.FCL.sf.os.graphics/blob/master/windowing/windowserver/nonnga/SERVER/CLIENT.CPP)

## SIS derleme

Windows üzerinde Qt SDK 1.2.1, Symbian^3 Qt 4.7.4 kiti, SBSv2,
GCCE ve Symbian paketleme araçları gerekir. Mevcut `.pro` dosyası
paketleme yollarında varsayılan `C:/QtSDK` kurulumunu kullanır.
Farklı bir kurulumda `.pro` içindeki iki SDK yolunu ayarlayın.

Qt for Symbian komut isteminde, depo kökünden başlayın.

```bat
cd apps\BelleMiddleKey
qmake BelleMiddleKey.pro -r -spec symbian-sbsv2 "CONFIG+=release"
make release-gcce
powershell -NoProfile -Command "(Get-Content BelleMiddleKey_template.pkg -Raw).Replace('$(PLATFORM)','armv5').Replace('$(TARGET)','urel') | Set-Content -Encoding Unicode BelleMiddleKey_elle.pkg"
if not exist ..\..\dist mkdir ..\..\dist
makesis BelleMiddleKey_elle.pkg BelleMiddleKey-unsigned.sis
signsis -s BelleMiddleKey-unsigned.sis ..\..\dist\BelleMiddleKey-Softkeys.sis "C:\QtSDK\Symbian\SDKs\Symbian3Qt474\src\s60installs\selfsigned.cer" "C:\QtSDK\Symbian\SDKs\Symbian3Qt474\src\s60installs\selfsigned.key"
```

SDK ile gelen örnek sertifikanın süresi dolmuşsa, `signsis` komutunda
geçerli bir geliştirme sertifikası ve ona ait anahtarı kullanın. Belle SDK,
GCCE 4.6.3 ve Qt 4.7.4 ile doğrulanan Linux derlemesinde paket ayrı bir
kendinden imzalı geliştirme sertifikasıyla imzalandı.

Mevcut kendinden imzalı kurulum, `SwEvent` nedeniyle telefonda projenin
belgelediği Install Server yamasını gerektirir. [Derleme](../../docs/BUILDING.md)
[Kurulum](../../docs/INSTALLING.md)

## Nokia E6 kontrolü

1. Mevcut `kisayollar.txt` dosyasını yedekleyin. Paketi kurduktan sonra
   eski eşlemelerin listelendiğini ve çalıştığını kontrol edin.
2. **Yeni kısayol** ile Rehber tek basışına **Sol Softkey (Seçenekler)**,
   Takvim tek basışına **Sağ Softkey (Geri / Çıkış)** atayın. Rehber
   uzun basışına **Uygulama aç… → Rehber** atayın.
3. Ayar ekranından çıkın ve servis çalışırken yerleşik bir uygulamada
   (ör. Dosya yöneticisi) ve üçüncü taraf bir uygulamada deneyin.
   Sol tuş ekrandaki sol komutu, sağ tuş ekrandaki sağ komutu bir kez
   tetiklemeli. Rehber'i basılı tutmak yalnızca Rehber'i açmalı.
4. Aynı fiziksel tuşa farklı tek, çift ve üçlü basış eylemleri tanımlayıp
   her dizinin yalnızca bir eylem tetiklediğini doğrulayın. Arayüzün
   **Test et** ekranı, fiziksel tuş kodlarını ve algılanan basışları gösterir.
5. Telefonu yeniden başlatıp atamaları tekrar deneyin. **Servisi durdur**
   seçeneğiyle Rehber ve Takvim tuşlarının normal işlevlerine döndüğünü
   kontrol edin.

Nokia E6 donanımında ve Belle sürümünde çalışma, bu kontrolle doğrulanmalıdır.
