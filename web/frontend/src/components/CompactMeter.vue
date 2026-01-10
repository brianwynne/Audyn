<template>
  <div class="compact-meter">
    <span class="channel-label">{{ channel.name }}</span>
    <div class="meter-bar">
      <div class="meter-background">
        <div
          class="meter-fill"
          :style="{ width: `${100 - levelPercent}%` }"
        />
        <div
          class="meter-peak"
          :style="{ left: `${peakPercent}%` }"
          :class="{ 'clipping': channel.clipping }"
        />
      </div>
    </div>
    <span class="level-value" :class="{ 'text-error': channel.clipping }">
      {{ channel.level_db.toFixed(0) }}
    </span>
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
</script>

<style scoped>
.compact-meter {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0;
}

.channel-label {
  font-weight: bold;
  font-size: 12px;
  width: 16px;
  text-align: center;
  color: rgba(255, 255, 255, 0.7);
}

.meter-bar {
  flex: 1;
}

.meter-background {
  height: 16px;
  background: linear-gradient(to right,
    #1b5e20 0%,
    #4caf50 50%,
    #ffc107 75%,
    #ff9800 85%,
    #f44336 95%,
    #d32f2f 100%
  );
  border-radius: 2px;
  position: relative;
  overflow: hidden;
}

.meter-fill {
  position: absolute;
  top: 0;
  right: 0;
  height: 100%;
  background: rgba(0, 0, 0, 0.75);
  transition: width 0.05s linear;
}

.meter-peak {
  position: absolute;
  top: 0;
  width: 2px;
  height: 100%;
  background: white;
  transition: left 0.1s ease-out;
}

.meter-peak.clipping {
  background: #f44336;
  box-shadow: 0 0 6px #f44336;
}

.level-value {
  font-family: monospace;
  font-size: 11px;
  width: 28px;
  text-align: right;
  color: rgba(255, 255, 255, 0.7);
}
</style>
