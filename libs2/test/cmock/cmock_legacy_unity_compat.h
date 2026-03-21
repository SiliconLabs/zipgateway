#ifndef CMOCK_LEGACY_UNITY_COMPAT_H
#define CMOCK_LEGACY_UNITY_COMPAT_H

#ifndef UNITY_SET_DETAIL
#define UNITY_SET_DETAIL(msg) ((void)(msg))
#endif

#ifndef UNITY_CLR_DETAILS
#define UNITY_CLR_DETAILS() ((void)0)
#endif

#ifndef UNITY_SET_DETAILS
#define UNITY_SET_DETAILS(msg1, msg2) ((void)(msg1), (void)(msg2))
#endif

#endif
