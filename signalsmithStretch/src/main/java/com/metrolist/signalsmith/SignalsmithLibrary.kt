package com.metrolist.signalsmith

import androidx.annotation.OptIn
import androidx.media3.common.util.LibraryLoader
import androidx.media3.common.util.UnstableApi

@OptIn(UnstableApi::class)
object SignalsmithLibrary {
    private val loader =
        object : LibraryLoader("signalsmithStretchJNI") {
            override fun loadLibrary(name: String) {
                System.loadLibrary(name)
            }
        }

    fun isAvailable(): Boolean = loader.isAvailable
}
