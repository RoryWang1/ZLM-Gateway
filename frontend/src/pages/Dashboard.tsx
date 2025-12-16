import { Row, Col, Typography } from 'antd';
import { useQuery } from '@tanstack/react-query';
import StatsCard from '@/components/dashboard/StatsCard';
import ProtocolChart from '@/components/dashboard/ProtocolChart';
// import ResourceChart from '@/components/dashboard/ResourceChart';
import HookStatsCard from '@/components/dashboard/HookStatsCard';
import StateMachineVisualization from '@/components/dashboard/StateMachineVisualization';
import { getDashboardData, getStatusStatistics } from '@/api/monitoring';
import Loading from '@/components/common/Loading/Loading';

const { Title } = Typography;

export default function Dashboard() {
  const { data: dashboardData, isLoading: dashboardLoading } = useQuery({
    queryKey: ['dashboard'],
    queryFn: getDashboardData,
    refetchInterval: 30000, // 降低到30秒，进一步减少服务器负载和内存占用
    staleTime: 20000, // 20秒内数据视为新鲜
    gcTime: 2 * 60 * 1000, // 2分钟后清理缓存
    retry: 1,
  });

  const { data: statusStatistics, isLoading: statusStatisticsLoading } = useQuery({
    queryKey: ['status-statistics'],
    queryFn: getStatusStatistics,
    refetchInterval: 30000, // 降低到30秒，进一步减少服务器负载和内存占用
    staleTime: 20000, // 20秒内数据视为新鲜
    gcTime: 2 * 60 * 1000, // 2分钟后清理缓存
    retry: 1,
  });

  if (dashboardLoading) {
    return <Loading />;
  }

  const systemStats = dashboardData?.system_stats;
  const protocolStats = dashboardData?.protocol_stats || [];

  return (
    <div>
      <Title level={2}>仪表板</Title>



      {/* Hook 统计信息 */}
      <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
        <Col xs={24}>
          <HookStatsCard stats={dashboardData?.hook_stats} loading={dashboardLoading} />
        </Col>
      </Row>

      {/* 状态机可视化 */}
      <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
        <Col xs={24}>
          <StateMachineVisualization stats={statusStatistics} loading={statusStatisticsLoading} />
        </Col>
      </Row>

      {/* 图表区域 */}
      <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
        <Col xs={24} lg={12}>
          <ProtocolChart data={protocolStats} loading={dashboardLoading} />
        </Col>
        {/* 隐藏系统资源图表 - 暂时不展示 */}
        {/* <Col xs={24} lg={12}>
          <ResourceChart data={systemStats!} loading={dashboardLoading} />
        </Col> */}
      </Row>
    </div>
  );
}
