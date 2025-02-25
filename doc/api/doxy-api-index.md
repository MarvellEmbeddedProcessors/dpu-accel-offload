API
===

<!--
  SPDX-License-Identifier: Marvell-MIT
  Copyright (c) 2024 Marvell.
-->

The public API headers are grouped by topics

- **common**

  - [version]             (@ref dao_version.h)
  - [log]                 (@ref dao_log.h)
  - [utils]               (@ref dao_util.h)
  - [dynamic_string]      (@ref dao_dynamic_string.h)
  - [bitmap]              (@ref dao_bitmap.h)
  - [assert]              (@ref dao_assert.h)

- **workers**

  - [workers]             (@ref dao_workers.h)

- **netlink**

  - [notification infra]  (@ref dao_netlink.h)
  - [route]               (@ref dao_netlink_route.h)
  - [xfrm]                (@ref dao_netlink_xfrm.h)

- **portgroup**

  - [port_group]          (@ref dao_port_group.h)
  - [portq_group]         (@ref dao_portq_group.h)
  - [portq_group_worker]  (@ref dao_portq_group_worker.h)

- **flow**
  - [flow]                (@ref dao_flow.h)

- **dma helper**
  - [dma helper]          (@ref dao_dma.h)

- **pem**
  - [pem]                 (@ref dao_pem.h)

- **vfio**
  - [vfio]                (@ref dao_vfio.h)

- **virtio**
  - [virtio]              (@ref dao_virtio.h)
  - [virtio_net]          (@ref dao_virtio_netdev.h)

- **platform abstraction layer**
  - [pal]              (@ref dao_pal.h)

- **feature_arc**
  - [feature_arc]         (@ref dao_graph_feature_arc.h)
  - [feature_arc_worker]  (@ref dao_graph_feature_arc_worker.h)
