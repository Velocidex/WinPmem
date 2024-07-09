package winpmem

import (
	"fmt"
	"time"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/svc"
	"golang.org/x/sys/windows/svc/mgr"
)

func UninstallDriver(
	driver_path, service_name string,
	logger Logger) error {

	// A service already exists - we need to delete it and
	// recreate it to make sure it is set up correctly.
	pres, err := checkServiceExists(service_name)
	if err != nil {
		logger.Info("checkServiceExists: %v", err)
		return err
	}

	if !pres {
		return nil
	}

	// First stop the service
	err = controlService(service_name, svc.Stop, svc.Stopped)
	if err != nil {
		logger.Info("Could not stop service %s: %v", service_name, err)
	} else {
		logger.Info("Stopped service %s", service_name)
	}

	err = removeService(service_name)
	if err != nil {
		return fmt.Errorf("Remove old service: %w", err)
	}
	return nil
}

func InstallDriver(
	driver_path, service_name string,
	logger Logger) error {

	err := UninstallDriver(driver_path, service_name, logger)
	if err != nil {
		return err
	}

	err = installService(service_name, driver_path, logger)
	if err != nil {
		return fmt.Errorf("Install service: %w", err)
	}

	logger.Info("Installed service %s", service_name)

	// Since we stopped the service here, we need to make sure it
	// is started again.
	err = startService(service_name)

	// We can not start the service - everything is messed
	// up! Just die here.
	if err != nil {
		return err
	}
	logger.Info("Started service %s", service_name)

	return nil

}

func checkServiceExists(name string) (bool, error) {
	m, err := mgr.Connect()
	if err != nil {
		return false, err
	}
	defer m.Disconnect()
	s, err := m.OpenService(name)
	if err == nil {
		s.Close()
		return true, nil
	}

	return false, nil
}

func installService(
	service_name string,
	executable string,
	logger Logger) error {

	m, err := mgr.Connect()
	if err != nil {
		return err
	}
	defer m.Disconnect()

	logger.Info("Creating service %v", service_name)

	s, err := m.CreateService(
		service_name,
		executable,
		mgr.Config{
			ServiceType:  windows.SERVICE_KERNEL_DRIVER,
			StartType:    windows.SERVICE_DEMAND_START,
			ErrorControl: windows.SERVICE_ERROR_NORMAL,
			DisplayName:  service_name,
			Description:  service_name,
		})
	if err != nil {
		return err
	}
	defer s.Close()

	return nil
}

func startService(name string) error {
	m, err := mgr.Connect()
	if err != nil {
		return err
	}
	defer m.Disconnect()

	s, err := m.OpenService(name)
	if err != nil {
		return fmt.Errorf("could not access service: %v", err)
	}
	defer s.Close()

	err = s.Start("service", "run")
	if err != nil {
		return fmt.Errorf("could not start service: %v", err)
	}
	return nil
}

func removeService(name string) error {
	m, err := mgr.Connect()
	if err != nil {
		return err
	}
	defer m.Disconnect()

	s, err := m.OpenService(name)
	if err != nil {
		return fmt.Errorf("service %s is not installed: %w", name, err)
	}
	defer s.Close()

	err = s.Delete()
	if err != nil {
		return err
	}
	return nil
}

func controlService(name string, c svc.Cmd, to svc.State) error {
	m, err := mgr.Connect()
	if err != nil {
		return err
	}
	defer m.Disconnect()
	s, err := m.OpenService(name)
	if err != nil {
		return fmt.Errorf("could not access service: %v", err)
	}
	defer s.Close()
	status, err := s.Control(c)
	if err != nil {
		return fmt.Errorf("could not send control=%d: %v", c, err)
	}
	timeout := time.Now().Add(10 * time.Second)
	for status.State != to {
		if timeout.Before(time.Now()) {
			return fmt.Errorf("timeout waiting for service to go to state=%d", to)
		}
		time.Sleep(300 * time.Millisecond)
		status, err = s.Query()
		if err != nil {
			return fmt.Errorf("could not retrieve service status: %v", err)
		}
	}
	return nil
}
