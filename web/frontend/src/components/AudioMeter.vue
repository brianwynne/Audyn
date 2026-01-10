<template>
  <div class="audio-meter">
    <div class="d-flex align-center">
      <span class="channel-label mr-3">{{ channel.name }}</span>

      <div class="meter-container flex-grow-1">
        <div class="meter-background">
          <div
            class="meter-fill"
            :style="{ width: `${levelPercent}%` }"
            :class="meterClass"
          />
          <div
            class="meter-peak"
            :style="{ left: `${peakPercent}%` }"
            :class="{ 'clipping': channel.clipping }"
          />
        </div>

        <!-- Scale markers -->
        <div class="meter-scale">
          <span>-60</span>
          <span>-40</span>
          <span>-20</span>
          <span>-10</span>
          <span>-6</span>
          <span>-3</span>
          <span>0</span>
        </div>
      </div>

      <span class="level-value ml-3" :class="{ 'text-error': channel.clipping }">
        {{ channel.level_db.toFixed(1) }} dB
      </span>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  channel: {
    type: Object,
    required: true
  }
})

// Convert dB to percentage (0-100)
// -60 dB = 0%, 0 dB = 100%
const levelPercent = computed(() => {
  const db = Math.max(-60, Math.min(0, props.channel.level_db))
  return ((db + 60) / 60) * 100
})

const peakPercent = computed(() => {
  const db = Math.max(-60, Math.min(0, props.channel.peak_db))
  return ((db + 60) / 60) * 100
})

const meterClass = computed(() => {
  if (props.channel.clipping) return 'clip'
  if (props.channel.level_db > -6) return 'peak'
  if (props.channel.level_db > -12) return 'high'
  return 'normal'
})
</script>

<style scoped>
.audio-meter {
  padding: 8px 0;
}

.channel-label {
  font-weight: bold;
  font-size: 14px;
  width: 24px;
  text-align: center;
}

.meter-container {
  position: relative;
}

.meter-background {
  height: 24px;
  background: linear-gradient(to right,
    #1b5e20 0%,
    #4caf50 50%,
    #ffc107 75%,
    #ff9800 85%,
    #f44336 95%,
    #d32f2f 100%
  );
  border-radius: 4px;
  position: relative;
  overflow: hidden;
}

.meter-fill {
  position: absolute;
  top: 0;
  right: 0;
  height: 100%;
  background: rgba(0, 0, 0, 0.7);
  transition: width 0.05s linear;
}

.meter-peak {
  position: absolute;
  top: 0;
  width: 3px;
  height: 100%;
  background: white;
  transition: left 0.1s ease-out;
}

.meter-peak.clipping {
  background: #f44336;
  box-shadow: 0 0 8px #f44336;
}

.meter-scale {
  display: flex;
  justify-content: space-between;
  font-size: 10px;
  color: rgba(255, 255, 255, 0.5);
  margin-top: 2px;
  padding: 0 2px;
}

.level-value {
  font-family: monospace;
  font-size: 14px;
  width: 70px;
  text-align: right;
}
</style>
