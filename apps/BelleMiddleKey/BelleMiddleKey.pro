# BelleMiddleKey (eski OrtaTus + TusTest) - istenen tusa istenen kisayolu atar (Nokia E6, Symbian Belle, Qt 4.7.4)
#   BelleMiddleKey.exe       : ayar uygulamasi (Qt, yalnizca acikken bellekte)
#   BelleMiddleKeyServis.exe : Qt'siz arka plan servisi, telefon acilisinda baslar (servis/servis.mmp)
# Tetikler: bas-cek, pes pese 2-3 kez, basili tut. Eylemler: uygulama ac, Sessiz <-> profil,
# acik uygulamalari kapat, ana menu, gorev degistirici.
#
# SwEvent yetkisi kendinden imzali pakette "auto-patch" ile kaldirilamadigi icin SIS elle olusturulur
# (makesis + signsis, docs/BUILDING.md). Kurulum icin telefonda Install Server yamasi gerekir.

TARGET = BelleMiddleKey
TEMPLATE = app
QT += core gui

SOURCES += main.cpp ortak/motor.cpp ortak/eylem.cpp
HEADERS += ortak/motor.h ortak/eylem.h
INCLUDEPATH += ortak
OTHER_FILES += servis/servis.cpp servis/servis.mmp E39CF232.rss

symbian {
    TARGET.UID3 = 0xE39CF232
    # SwEvent: tus yakalama/benzetme, servise olay gonderme; Read/WriteDeviceData: profil; PowerMgmt: uygulama kapatma
    TARGET.CAPABILITY += SwEvent WriteDeviceData ReadDeviceData PowerMgmt
    INCLUDEPATH += /epoc32/include/app /epoc32/include/platform/app /epoc32/include/mw /epoc32/include/platform/mw /epoc32/include/platform
    LIBS += -lcone -lws32 -lapparc -lapgrfx -lprofileengine -lhwrmvibraclient -lhal -leuser -lefsrv

    # Arka plan servisi ayni bld.inf ile derlenir ve ayni pakete girer
    BLD_INF_RULES.prj_mmpfiles += servis/servis.mmp
    servis.pkg_postrules = "\"C:/QtSDK/Symbian/SDKs/Symbian3Qt474/epoc32/release/armv5/urel/BelleMiddleKeyServis.exe\" - \"!:/sys/bin/BelleMiddleKeyServis.exe\""
    DEPLOYMENT += servis

    # Servisi telefon acilisinda baslatma (Startup List Management)
    MMP_RULES += "SOURCEPATH ." "START RESOURCE E39CF232.rss" "TARGETPATH /private/101f875a/import" "END"
    startup.pkg_postrules = "\"C:/QtSDK/Symbian/SDKs/Symbian3Qt474/epoc32/data/z/private/101f875a/import/E39CF232.rsc\" - \"c:/private/101f875a/import/[E39CF232].rsc\""
    DEPLOYMENT += startup

    ICON = BelleMiddleKey.svg
    VERSION = 2.0.1
    DEPLOYMENT.display_name = BelleMiddleKey
    vendorinfo = "%{\"BelleMiddleKey\"}" ":\"BelleMiddleKey\""
    my_deployment.pkg_prerules += vendorinfo
    DEPLOYMENT += my_deployment
}

# Dil destegi (Turkce / English): kutuphane/dil
include($$PWD/../kutuphane/dil/dil.pri)
