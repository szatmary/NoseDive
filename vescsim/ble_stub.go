//go:build !darwin

package main

import "fmt"

// StartBLE is not supported on non-darwin platforms.
func (s *Simulator) StartBLE(name string) error {
	return fmt.Errorf("BLE peripheral mode is only supported on macOS")
}
