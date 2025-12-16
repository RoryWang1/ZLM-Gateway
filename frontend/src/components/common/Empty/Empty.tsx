import { Empty as AntEmpty } from 'antd';

interface EmptyProps {
  description?: string;
  image?: React.ReactNode;
}

export default function Empty({ description = '暂无数据', image }: EmptyProps) {
  return <AntEmpty description={description} image={image} />;
}

