# rkmpp

This is a crate for encoding video on Rockchip devices using the Rockchip MPP.

## Runtime Requirements

This crate dynamically loads the Rockchip MPP library, `librockchip_mpp.so`. This library is not included in this crate and must be installed on the system.

You also must ensure that `/dev/mpp_service` is present and accessible to the user running the application. By default, it may be accessible only to run. To change this, you can use a command like...

```bash
sudo chmod 666 /dev/mpp_service
```
