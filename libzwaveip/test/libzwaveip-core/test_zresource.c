/*
 * Copyright 2020 Silicon Laboratories Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "zresource.h"
#include "zresource-internal.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_zresource_add_remove(void)
{
  struct zip_service* local = zresource_add_service("local");
  struct zip_service* local_found = find_service_by_service_name("local");
  TEST_ASSERT_EQUAL(local, local_found);

  zresource_remove_service("local");
  local_found = find_service_by_service_name("local");
  TEST_ASSERT_NULL(local_found);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_zresource_add_remove);
  return UNITY_END();
}
