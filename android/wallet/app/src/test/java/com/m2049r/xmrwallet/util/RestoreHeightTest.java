/*
 * Copyright (c) 2017 m2049r
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.m2049r.xmrwallet.util;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

// Real Monzero mainnet heights at 00:00 UTC (node.monzero.org, 2026-10-24).
// A restore height above the real chain would skip a wallet's first funds,
// so every estimate must stay at or below these.
public class RestoreHeightTest {
    @Test
    public void beforeLaunchIsZero() {
        assertEquals(0, getHeight("1958-12-01"));
        assertEquals(0, getHeight("2025-12-31"));
        assertEquals(0, getHeight("2026-07-31"));
        assertEquals(0, getHeight("2026-08-25"));
        assertEquals(0, getHeight("2026-08-31"));
    }

    @Test
    public void neverAboveTheChain() {
        assertTrue(getHeight("2026-09-01") <= 1043);
        assertTrue(getHeight("2026-09-15") <= 1043 + 14 * 720);
        assertTrue(getHeight("2026-10-01") <= 1946);
        assertTrue(getHeight("2026-10-05") <= 1946);
        // 15,571 blocks by 2026-10-24 16:10 UTC: about 590 a day since 1 October
        assertTrue(getHeight("2026-10-24") <= 15571);
    }

    @Test
    public void growsWithTheDate() {
        assertTrue(getHeight("2026-10-10") < getHeight("2026-10-20"));
        assertTrue(getHeight("2026-10-20") < getHeight("2027-01-01"));
    }

    private long getHeight(String date) {
        return RestoreHeight.getInstance().getHeight(date);
    }
}
