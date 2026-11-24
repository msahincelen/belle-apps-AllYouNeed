# BelleGram - Nokia E6 (Symbian Belle, Qt 4.7.4) icin dogrudan Telegram istemcisi. Sunucu kullanmaz.
# MTProto 2.0, TL katman 233, sifreleme (SHA-256/512, AES-256-IGE, RSA, DH, SRP) tg/ altinda.
# Sesli mesajlar: libopus (third_party/opus, BSD). Konum: kutuphane/harita.
# Uygulama kimligi (api_id/api_hash) koda GOMULU DEGIL: kullanici ilk acilista kendi my.telegram.org
# degerlerini girer (ozel klasorde kimlik.ini). gizli/ ve test/ yalnizca masaustu testi icin, yayina girmez.

TARGET = BelleGram
TEMPLATE = app
QT += declarative network

INCLUDEPATH += tg
SOURCES += main.cpp cihaz.cpp bildirim.cpp tg/sifre.cpp tg/buyuksayi.cpp tg/tl.cpp tg/mtproto.cpp tg/telegram.cpp tg/opusses.cpp
HEADERS += cihaz.h bildirim.h tg/mtproto.h tg/telegram.h tg/tl.h tg/sifre.h tg/buyuksayi.h tg/opusses.h
RESOURCES += resources.qrc tl/tl.qrc
OTHER_FILES += qml/*.qml qml/*.js

include($$PWD/third_party/opus/opus.pri)

symbian {
    TARGET.UID3 = 0xE39CF23F
    # NetworkServices: internet, UserEnvironment: mikrofon (sesli mesaj)
    TARGET.CAPABILITY += NetworkServices UserEnvironment
    CONFIG += mobility
    MOBILITY += multimedia
    DEFINES += BG_SES
    # Bildirimler: ust kose kutusu (avkon), titresim, durum cubugu cekmecesi (Pigler, GPL-3.0)
    INCLUDEPATH += /epoc32/include/mw /epoc32/include/platform/mw /epoc32/include/platform
    LIBS += -lavkon -laknskins -leikcore -lcone -lws32 -lapgrfx -lhwrmvibraclient -leuser -laknnotify -lhal
    include($$PWD/../kutuphane/pigler/qt-library/pigler.pri)
    # Pigler eklentisi SIS'e gomulu (tek kurulum; WhatsBelle ile ayni eklenti, zaten kuruluysa atlanir)
    pigler_embed.pkg_postrules = "@\"$$PWD/../kutuphane/pigler/sis_hazir/Pigler.sis\",(0x20292B69)"
    DEPLOYMENT += pigler_embed
    # TL semasi (~2600 tanim) + QML: yigin genis
    TARGET.EPOCHEAPSIZE = 0x100000 0x4000000
    TARGET.EPOCSTACKSIZE = 0x14000
    ICON = BelleGram.svg
    VERSION = 0.3.1
    DEPLOYMENT.display_name = BelleGram
    vendorinfo = "%{\"BelleGram\"}" ":\"BelleGram\""
    my_deployment.pkg_prerules += vendorinfo
    DEPLOYMENT += my_deployment
}

# Konum gonderme / gosterme: harita, karo onbellegi, GPS (kutuphane/harita)
include($$PWD/../kutuphane/harita/harita.pri)
# Dil destegi (Turkce / English): kutuphane/dil
include($$PWD/../kutuphane/dil/dil.pri)
include($$PWD/../kutuphane/depo/depo.pri)   # depolama yeri (kurulum surucusu / kart / E / C)

# Ortak pano yardimcisi (Ctrl+V: QML paste() E6da calismiyor)
include($$PWD/../kutuphane/pano/pano.pri)
