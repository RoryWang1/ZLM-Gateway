// GB28181设备添加/编辑表单组件

import { Modal, Form, Input, message } from 'antd';
import { useAddGB28181Device } from '@/hooks/useDevices';
import type { Device } from '@/types/device';

interface GB28181DeviceFormProps {
  open: boolean;
  onCancel: () => void;
  onSuccess?: () => void;
  initialValues?: Partial<Device>;
}

export default function GB28181DeviceForm({ 
  open, 
  onCancel, 
  onSuccess,
  initialValues 
}: GB28181DeviceFormProps) {
  const [form] = Form.useForm();
  const addDeviceMutation = useAddGB28181Device();

  const handleSubmit = async () => {
    try {
      const values = await form.validateFields();
      await addDeviceMutation.mutateAsync({
        id: values.id,  // 后端期望的是 'id' 而不是 'device_id'
        name: values.name,
        ip: values.ip,
        port: values.port,
        manufacturer: values.manufacturer,
        model: values.model,
        protocol: 'gb28181',
        username: values.username,
        password: values.password,
      } as any);
      
      form.resetFields();
      if (onSuccess) {
        onSuccess();
      }
      onCancel();
    } catch (error: any) {
      if (error.errorFields) {
        // 表单验证错误
        return;
      }
      message.error(`操作失败: ${error.message}`);
    }
  };

  return (
    <Modal
      title={initialValues ? '编辑GB28181设备' : '添加GB28181设备'}
      open={open}
      onOk={handleSubmit}
      onCancel={() => {
        form.resetFields();
        onCancel();
      }}
      okText="保存"
      cancelText="取消"
      width={600}
      confirmLoading={addDeviceMutation.isPending}
    >
      <Form
        form={form}
        layout="vertical"
        initialValues={initialValues || {
          port: 5060,
        }}
      >
        <Form.Item
          name="id"
          label="设备ID"
          rules={[
            { required: true, message: '请输入设备ID' },
            { len: 20, message: '设备ID必须是20位国标ID' },
          ]}
        >
          <Input placeholder="20位国标ID，例如：34020000001320000001" disabled={!!initialValues} />
        </Form.Item>
        <Form.Item name="name" label="设备名称">
          <Input placeholder="设备名称" />
        </Form.Item>
        <Form.Item
          name="ip"
          label="IP地址"
          rules={[
            { required: true, message: '请输入IP地址' },
            { type: 'string', pattern: /^(\d{1,3}\.){3}\d{1,3}$/, message: '请输入有效的IP地址' },
          ]}
        >
          <Input placeholder="192.168.1.100" />
        </Form.Item>
        <Form.Item
          name="port"
          label="SIP端口"
          rules={[
            { required: true, message: '请输入端口' },
            { type: 'number', min: 1, max: 65535, message: '端口范围：1-65535' },
          ]}
        >
          <Input type="number" placeholder="5060" />
        </Form.Item>
        <Form.Item name="manufacturer" label="制造商">
          <Input placeholder="制造商" />
        </Form.Item>
        <Form.Item name="model" label="型号">
          <Input placeholder="型号" />
        </Form.Item>
        <Form.Item name="username" label="用户名（SIP认证）">
          <Input placeholder="SIP认证用户名（可选）" />
        </Form.Item>
        <Form.Item name="password" label="密码（SIP认证）">
          <Input.Password placeholder="SIP认证密码（可选）" />
        </Form.Item>
      </Form>
    </Modal>
  );
}

