package com.m2049r.xmrwallet.dex;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import org.junit.Test;

import java.io.File;
import java.nio.file.Files;
import java.security.SecureRandom;
import java.util.List;

/** The trading keys' recovery words are standard BIP-39: the published test vectors must hold. */
public class DexMnemonicTest {
    private static List<String> list() throws Exception {
        File f = new File("src/main/assets/dex/bip39-english.txt");
        if (!f.isFile()) f = new File("app/src/main/assets/dex/bip39-english.txt");
        final List<String> words = Files.readAllLines(f.toPath());
        assertEquals(2048, words.size());
        return words;
    }

    @Test
    public void zeroEntropyGivesThePublishedWordsAndSeed() throws Exception {
        final String words = DexMnemonic.words(new byte[16], list());
        assertEquals("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about", words);
        // BIP-39 seed with no extra passphrase (as every EVM wallet derives it)
        assertEquals("5eb00bbddcf069084889a8ab9155568165f5c453ccb85e70811aaed6f6da5fc19a5ac40b389cd370d086206dec8aa6c43daea6690f20ad3d8d48b2d2ce9e38e4",
                DexFiles.hex(DexMnemonic.seed(words)));
    }

    @Test
    public void theCommonTestPhraseGivesTheSeedTheTradeHelperTestsUse() throws Exception {
        // The same seed as gui/dex-contracts/engine/src/evm_key.rs checks (account 0xf39F...2266)
        final String words = "test test test test test test test test test test test junk";
        assertNotNull(DexMnemonic.entropy(words, list()));
        assertEquals("9dfc3c64c2f8bede1533b6a79f8570e5943e0b8fd1cf77107adf7b72cef42185d564a3aee24cab43f80e3c4538087d70fc824eabbad596a23c97b6ee8322ccc0",
                DexFiles.hex(DexMnemonic.seed(words)));
    }

    @Test
    public void wordsAndEntropyRoundTrip() throws Exception {
        final SecureRandom random = new SecureRandom();
        for (int i = 0; i < 200; i++) {
            final byte[] entropy = new byte[16];
            random.nextBytes(entropy);
            final String words = DexMnemonic.words(entropy.clone(), list());
            assertEquals(12, words.split(" ").length);
            assertArrayEquals(entropy, DexMnemonic.entropy("  " + words.toUpperCase() + " ", list()));
        }
    }

    @Test
    public void aWrongWordOrChecksumIsRefused() throws Exception {
        assertNull(DexMnemonic.entropy("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon", list()));
        assertNull(DexMnemonic.entropy("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon notaword", list()));
        assertNull(DexMnemonic.entropy("abandon about", list()));
    }

    @Test
    public void amountsAreConvertedExactly() {
        assertEquals("1000000000000000", DexFiles.atomic("0.001", 18));
        assertEquals("2000000000000", DexFiles.atomic("20", 11));
        assertNull(DexFiles.atomic("0", 11));
        assertNull(DexFiles.atomic("0.0000000000001", 11));   // more decimals than the coin has
        assertNull(DexFiles.atomic("1e3", 11));
        assertEquals("0.001", DexFiles.units("1000000000000000", 18));
        assertEquals("20", DexFiles.units("2000000000000", 11));
        assertEquals("0", DexFiles.units("0", 11));
        assertEquals("0.00000000001", DexFiles.units("1", 11));
    }
}
