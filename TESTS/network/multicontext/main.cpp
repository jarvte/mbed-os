/*
 * Copyright (c) 2020, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if !defined(MBED_CONF_TARGET_NETWORK_DEFAULT_INTERFACE_TYPE) || \
    MBED_CONF_TARGET_NETWORK_DEFAULT_INTERFACE_TYPE != CELLULAR
#error [NOT_SUPPORTED] No network configuration found for this target.
#elif !defined(MBED_CONF_RTOS_PRESENT)
#error [NOT_SUPPORTED] network interface test cases require a RTOS to run.
#else
#include "greentea-client/test_env.h"
#include "unity/unity.h"
#include "utest.h"
#include "utest/utest_stack_trace.h"
#include "CellularContext.h"
#include "CellularDevice.h"
#include "UDPSocket.h"

using namespace utest::v1;
using namespace mbed;
using namespace rtos;

const char *CTX1_MSG = "Context testing 1";
const char *CTX2_MSG = "Testing context 2";
const char *CTX3_MSG = "wuhuuu jippiii";

Thread *thread1;
Thread *thread2;
Thread *thread3;

// Echo server hostname
const char *host_name = "echo.mbedcloudtesting.com";

// Echo server port (same for TCP and UDP)
const int port = 7;

CellularInterface *interface = NULL;
CellularContext *ctx1 = NULL;
CellularContext *ctx2 = NULL;
CellularContext *ctx3 = NULL;
UDPSocket sock1;
UDPSocket sock2;
UDPSocket sock3;
CellularDevice *dev = NULL;

static void init()
{
    interface = CellularInterface::get_default_instance();
    TEST_ASSERT_NOT_NULL(interface);
    ctx1 = (CellularContext*)interface;
    dev = ctx1->get_device();
    TEST_ASSERT_NOT_NULL(dev);
    ctx2 = dev->create_context(&dev->get_file_handle());
    TEST_ASSERT_NOT_NULL(ctx2);
    ctx3 = dev->create_context(&dev->get_file_handle());
    TEST_ASSERT_NOT_NULL(ctx3);
}

static void deinit()
{
    if (interface != NULL && dev != NULL) {
        dev->delete_context(ctx1);
        dev->delete_context(ctx2);
        dev->delete_context(ctx3);
    }
}

void connect_ctx(CellularContext *ctx)
{
#if defined(MBED_CONF_APP_WIFI_SECURE_SSID)
        char ssid[SSID_MAX_LEN + 1] = MBED_CONF_APP_WIFI_SECURE_SSID;
        char pwd[PWD_MAX_LEN + 1] = MBED_CONF_APP_WIFI_PASSWORD;
        nsapi_security_t security = NSAPI_SECURITY_WPA_WPA2;

#elif defined(MBED_CONF_APP_WIFI_UNSECURE_SSID)
        char ssid[SSID_MAX_LEN + 1] = MBED_CONF_APP_WIFI_UNSECURE_SSID;
        char pwd[PWD_MAX_LEN + 1] = NULL;
        nsapi_security_t security = NSAPI_SECURITY_NONE;
#endif

}

void test_send_recv(void *ctx_num)
{
    nsapi_size_or_error_t retcode;
    CellularContext *ctx;
    UDPSocket socket;
    SocketAddress sock_addr;
    const void *data = NULL;
    int datalen = 0;
    char recv_buf[40];
    int ctx_number = *(int *)ctx_num;

    switch (ctx_number) {
        case 1:
            ctx = ctx1;
            socket = sock1;
            data = (const void*)CTX1_MSG;
            datalen = strlen(CTX1_MSG);
            break;
        case 2:
            ctx = ctx2;
            socket = sock2;
            data = (const void*)CTX2_MSG;
            datalen = strlen(CTX2_MSG);
            break;
        case 3:
            ctx = ctx3;
            socket = sock3;
            data = (const void*)CTX3_MSG;
            datalen = strlen(CTX3_MSG);
            break;
        default:
            TEST_ASSERT_MESSAGE(false, "Invalid Context number");
            break;
    }

    retcode = socket.open(ctx);
    TEST_ASSERT_EQUAL(0, retcode);

    socket.set_timeout(15000);

    retcode = ctx->gethostbyname(host_name, &sock_addr);
    TEST_ASSERT_EQUAL(0, retcode);

    sock_addr.set_port(port);

    retcode = socket.sendto(sock_addr, (const void*)data, datalen);
    TEST_ASSERT_EQUAL(0, retcode);

    retcode = socket.recvfrom(&sock_addr, (void*) recv_buf, sizeof(recv_buf));
    TEST_ASSERT_EQUAL(sizeof(recv_buf), retcode);

    socket.close();
}

void multicontext_synchronous()
{
    int ctx_num = 1;
    thread1 = new Thread(osPriorityNormal, OS_STACK_SIZE, nullptr, "thread 1");
    TEST_ASSERT_EQUAL(osOK, thread1->start(callback(test_send_recv, &ctx_num)));
/*
    thread2 = new Thread(osPriorityNormal, OS_STACK_SIZE, nullptr, "thread 2");
    TEST_ASSERT_EQUAL(osOK, thread2->start(callback(test_send_recv, 2)));

    thread2 = new Thread(osPriorityNormal, OS_STACK_SIZE, nullptr, "thread 3");
    TEST_ASSERT_EQUAL(osOK, thread2->start(callback(test_send_recv, 3)));*/


    thread1->join();
    delete thread1;
/*
    thread2->join();
    delete thread2;

    thread3->join();
    delete thread3;*/
}


// Test setup
utest::v1::status_t greentea_setup(const size_t number_of_cases)
{
    GREENTEA_SETUP(480, "default_auto");
    init();
    return greentea_test_setup_handler(number_of_cases);
}

void greentea_teardown(const size_t passed, const size_t failed, const failure_t failure)
{
    deinit();
    return greentea_test_teardown_handler(passed, failed, failure);
}

Case cases[] = {
    Case("MULTICONTEXT_SYNCHRONOUS_UDP ECHOTEST", multicontext_synchronous),
    /*Case("MULTIHOMING_ASYNCHRONOUS_DNS", MULTIHOMING_ASYNCHRONOUS_DNS),
    Case("MULTIHOMING_UDPSOCKET_ECHOTEST", MULTIHOMING_UDPSOCKET_ECHOTEST),
    Case("MULTIHOMING_UDPSOCKET_ECHOTEST_NONBLOCK", MULTIHOMING_UDPSOCKET_ECHOTEST_NONBLOCK),*/
};

Specification specification(greentea_setup, cases, greentea_teardown, greentea_continue_handlers);

int main()
{
    return !Harness::run(specification);
}

#endif // !defined(MBED_CONF_RTOS_PRESENT)
