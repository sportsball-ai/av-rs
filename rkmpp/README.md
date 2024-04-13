# rkmpp

This is a crate for encoding video on Rockchip devices using the Rockchip MPP.

## Runtime Requirements

This crate dynamically loads the Rockchip MPP library, `librockchip_mpp.so`. This library is not included in this crate and must be installed on the system.

### Permissions

You also must ensure that `/dev/mpp_service` is present and accessible to the user running the application. By default, it may be accessible only to the root user. To fix this...

Make sure the current user is in the `video` group:

```bash
groups
```

If they're not in the `video` group, add them:

```bash
sudo usermod -a -G video $USER
```

Then log out and log back in and check again that the user is in the `video` group.

You'll also need to make sure `/dev/mpp_service` is accessible to the `video` group:

```bash
ls -l /dev/mpp_service
```

If it's not, add a `udev` rule to permanently configure the permissions of the device:

```bash
echo 'KERNEL=="mpp_service", MODE="0660", GROUP="video"' | sudo tee /etc/udev/rules.d/99-mpp_service.rules
```

Reboot and confirm that the device is accessible to the `video` group.
