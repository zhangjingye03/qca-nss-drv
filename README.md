# qca-nss-drv

This is a code kernel driver for Qualcomm Network Sub System (NSS).

Pulled from [CodeAurora](https://us.codeaurora.org/cgit/quic/qsdk/oss/lklm/nss-drv), tag [NHSS.CC.1.0-00348-O](https://us.codeaurora.org/cgit/quic/qsdk/oss/lklm/nss-drv/tag/?h=NHSS.CC.1.0-00348-O).

# How to use

* Change to your OpenWrt DD Trunk's root directory

* Change to `./package/kernel`

* Clone this repo

* Back to the root directory, `make menuconfig`, select

```
Kernel modules --->
        Qualcomm Network Sub System (NSS) --->
            <M> kmod-qca-nss..................... Qualcomm NSS
```

* Build
